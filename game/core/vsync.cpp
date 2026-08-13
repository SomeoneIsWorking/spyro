// vsync.cpp — Spyro's vblank timebase.
//
// THE PROBLEM. libetc's VSync(mode) waits until the vblank counter at 0x800749E0 reaches a target,
// via the helper at 0x8005DD0C: `while ([0x800749E0] < a0) spin;` with an (a1<<15)-iteration budget,
// printing "VSync: timeout" when it runs out. On real hardware that counter is incremented by the
// VSync IRQ callback. This runtime raises no IRQs, so the counter is frozen at its initial value and
// EVERY VSync times out — which is what the boot log showed.
//
// WHY THIS CHOKEPOINT, and not VSync itself. VSync computes a return value from its own state (the
// GPU status mirror at [[0x800738BC]] and the hblank delta [[0x800738C0]]-[0x800738C4]), then polls
// GPU status after waiting. Reimplementing all of that natively would mean guessing a contract that
// the guest already implements correctly. The helper's contract, by contrast, is unambiguous:
// "do not return until the counter has reached a0". So we supply ONLY the thing the missing IRQ
// would have supplied — the counter advancing over time — and VSync's own logic, its return value
// and its GPU polling all run unmodified on the real recompiled body. That keeps the RE frontier
// honest: nothing here reimplements game or library behaviour, it restores a hardware timebase.
//
// WHY PRESENT HERE. One vblank waited == one frame displayed. The guest still owns its own frame
// loop in this phase (see game_hooks.cpp), so this wait is the only place per frame where the port
// knows a frame boundary has been reached. Presenting and pacing here is what gives the port its
// timebase; when the native frame loop later takes ownership, THAT loop owns present/pace and this
// handler goes back to being a pure counter advance.
#include "core.h"
#include "game.h"
#include "platform_hle.h"
#include "cfg.h"      // cfg_on — PSXPORT_REPL is a feature flag, not a diagnostic
#include "spyro_game.h"
#include <lucent/log.h>
#include "hle.h"      // Hle::deliverEvent — per-frame BIOS events
#include "recomp_iface.h"
#include "rec_decls.h"
#include "guest_call.h"   // rc0 — run a guest function to its `jr ra`
#include "snapshot.h"     // snapshot_tick — on-demand guest RAM capture at a frame boundary
#include "repl.h"         // class Repl — interactive inspection, pumped from this frame boundary
#include "producer_run.h" // spyro_producer_run_frame — the producer DB's per-frame boundary (issue #58)
#include <time.h>         // clock_gettime — wall clock on the PSXPORT_DEBUG=pace lines

namespace {

// ── The VBlank callback ────────────────────────────────────────────────────────────────────────────
// The boot's input setup at 0x800123C8 does three things in a row, and the third is the one that
// matters here:
//   0x800123E0  jal 0x8006B010          PadInitDirect(0x800786A0, 0x80078E50)  — libpad, direct SIO
//   0x80012434  jal 0x80053C68          call the pad decoder once, to prime its state
//   0x80012444  jal 0x8005DE58          VSyncCallback(0x80053C68)
// so the game's real pad decoder is not called from its frame loop at all — it is INSTALLED AS THE
// VBLANK INTERRUPT CALLBACK, and every subsequent invocation is an IRQ. This runtime raises no IRQs,
// so the decoder ran exactly once at boot and never again (measured: probe on 0x80053C68 logs "call
// #1" and nothing else in a 20s run). With it dead, the pad-class word [0x80077384] stays 0, the
// decoder's "no controller" arm is the only one ever taken, and the only thing that ever publishes
// input is the attract demo's playback path (C062) — which is why the port loops attract forever.
//
// So the fix is the same shape as the counter above: supply what the missing IRQ would have supplied.
// We do NOT reimplement the decoder — we run the guest's own registered callback body, once per
// vblank, which is exactly when the console would have run it.
//
// 0x8005DE58 is libetc's VSyncCallback(fn): it takes the handler in a0 and hands it to the interrupt
// callback table via [0x800749AC]+20 with a0=4 (the VSYNC slot). Intercepting it here — rather than
// hardcoding 0x80053C68 — means the port follows whatever the game registers, including a later
// re-registration, and it fails visibly (no callback recorded) rather than silently running a stale
// address if the boot path ever changes.
uint32_t g_vblank_cb = 0;

void vsync_callback_set(Core* c) {
  const uint32_t fn = c->r[4];
  if (fn != g_vblank_cb) {
    g_vblank_cb = fn;
    lucent::info("vsync", "VSyncCallback(0x{:08X}) registered — this is the per-vblank handler the "
                          "port must run, since no IRQ will.", fn);
  }
  gen_func_8005DE58(c);   // super-call: let libetc do its own table bookkeeping unmodified
}

// Run the registered vblank handler as an interrupt would: with the whole register file saved and
// restored. This is called from INSIDE a guest call (the wait helper below), so the handler running
// on the same Core would otherwise clobber the interrupted function's caller-saved registers — a
// real IRQ saves and restores them, and skipping that produces corruption that looks like a
// mistranslation rather than like this.
// libetc's ROOT-HANDLER table, indexed by IRQ number; slot 0 is VBLANK. The guest installs its own
// VBlank root handler here at boot (0x8005E560, via 0x8005E224), and that handler is what walks the
// 8-entry callback table at 0x800749C0 calling every registered slot.
constexpr uint32_t kRootHandlers = 0x80073928u;   // +0 = IRQ 0 = VBLANK

// Deliver the vblank the way the console does: run the guest's VBlank ROOT handler, not one hand-picked
// callback.
//
// WHY THIS CHANGED. This used to call only the handler captured from VSyncCallback — libetc callback
// SLOT 4. But slot 4 is one of EIGHT, and the game registers others: Spyro puts 0x80067CD4 in slot 7 at
// frame 835, and that handler is the sole setter of the flag its title screen polls every frame. With
// only slot 4 delivered, slot 7 never ran, so the title screen could never advance (issue 0027, C117).
// Special-casing one callback meant silently dropping every other one the game registers.
//
// Running the root handler fixes the whole class rather than that instance: it dispatches ALL eight
// slots, including any registered later, and it is the guest's own code doing it rather than our
// reimplementation of the loop.
//
// THE ROOT HANDLER ALSO OWNS THE VBLANK COUNTER ([0x800749E0]) — it increments it on entry. So when it
// runs, the port must NOT also maintain that counter; vblank_wait re-reads it instead. Returns true if
// the guest's handler ran, so the caller knows who owns the count this iteration.
//
// The register file is saved and restored around the call for the same reason as before: this runs from
// INSIDE a guest call, and a real IRQ preserves the interrupted function's registers.
//
// ── AND THE HANDLER DOES NOT RUN ON THE INTERRUPTED STACK ──────────────────────────────────────────
//
// $sp IS NOT A STACK POINTER ON A PSX AT AN ARBITRARY INSTRUCTION. The kernel's exception entry saves
// the interrupted context and switches to its own stack before running the interrupt chain, so a PS1
// function may legally keep scratch values in $sp/$gp/$fp, and hand-written renderers do — they are
// three more registers. Spyro's is one, and this is from the recompiled instructions, not inference:
// `gen_func_800258F0` (RenderWorldChunks, generated/shard_3.c) saves sp/gp/fp in its prologue,
// then writes `sp = -1`, `sp = 0x1F800000` (the scratchpad base) and `sp = r1 + 7680` in its inner
// loops, and reloads all three from its frame at the end.
//
// MEASURED CONSEQUENCE of getting this wrong, because it does not fail as a stack bug. With the host
// turn armed and the handler run on `c->r[29]`, the port died in RenderWorldChunks on an UNMAPPED
// read8 at 0x9006E9AB about half a second into the run. `PSXPORT_DEBUG=hostturn` showed why: turns
// 25-31 were taken at libetc function entries with sp=0x801FFFxx (a real stack), turn 32 was taken at
// a loop back-edge inside the renderer with sp=0x80071B00 and gp=0x80071D20 — mid-table addresses,
// not a stack — and the handler chain wrote its frames there, over libetc's data. The crash then
// surfaced somewhere else entirely, reading a pointer that had been overwritten.
//
// WHERE THE HANDLER STACK LIVES. In the kernel region, which is where the console's is: a PS-EXE
// loads at 0x80010000 (this game's boot log says so) precisely because everything below belongs to
// the BIOS, and psxport already carves its own BIOS work area out of the same region
// (hle.cpp `HLE_WORK_BASE` 0x8000E000 / `HLE_B0TABLE` 0x8000F000 / `HLE_C0TABLE` 0x8000F800). So this
// is the framework's existing convention rather than a new claim on guest memory.
//
// AND WHICH PART OF IT IS FREE IS MEASURED, not assumed. `PSXPORT_WWATCH=0x80000000,0x8000FFFC
// PSXPORT_WWATCH_BT=1` over a whole run from boot to the memory-card screen logs writes to exactly
// 131 distinct addresses in that 64 KB: 0x80000000-0x8000007F (from a B0 stub at 0x80068900) and
// psxport's own work area words at 0x8000F16C / 0x8000F800-0x8000F807. Nothing else in the region is
// ever written. The stack therefore sits directly below `HLE_WORK_BASE` and grows down, with the
// nearest measured neighbour ~48 KB further down.
constexpr uint32_t kHandlerStackTop   = 0x8000E000u;   // exclusive: first word used is TOP-4
constexpr uint32_t kHandlerStackBytes = 8192u;
constexpr uint32_t kHandlerStackFloor = kHandlerStackTop - kHandlerStackBytes;
// Written into every word of the stack once, so "how deep did the handler chain actually go" is a
// measurement and "did it run off the bottom" is a test rather than a hope. Any value works; this one
// is recognisable in a RAM dump and is not a plausible pointer or count.
constexpr uint32_t kStackPoison = 0xCDCDCDCDu;

void handler_stack_init(Core* c) {
  for (uint32_t a = kHandlerStackFloor; a < kHandlerStackTop; a += 4) c->mem_w32(a, kStackPoison);
  lucent::info("vsync", "vblank handler stack armed at [0x{:08X},0x{:08X}) — the guest's interrupt "
                        "handler does NOT run on the interrupted $sp (see vsync.cpp)",
               kHandlerStackFloor, kHandlerStackTop);
}

// How far down the handler chain has ever reached, as an address. Reported by `PSXPORT_DEBUG=vsync`;
// the floor word is checked unconditionally, because an overflow silently corrupts whatever is below
// and would otherwise present as a bug somewhere else entirely — which is exactly the failure this
// whole comment is about.
uint32_t handler_stack_low_water(Core* c) {
  for (uint32_t a = kHandlerStackFloor; a < kHandlerStackTop; a += 4)
    if (c->mem_r32(a) != kStackPoison) return a;
  return kHandlerStackTop;
}

bool run_vblank_callback(Core* c) {
  const uint32_t root = c->mem_r32(kRootHandlers);
  const uint32_t target = root ? root : g_vblank_cb;
  if (!target) return false;
  static bool armed = false;
  if (!armed) { armed = true; handler_stack_init(c); }

  R3000 saved = *static_cast<R3000*>(c);
  c->r[29] = kHandlerStackTop;          // the stack switch the kernel's exception entry performs
  rc0(c, target);
  *static_cast<R3000*>(c) = saved;      // …and the context restore its exit performs

  if (c->mem_r32(kHandlerStackFloor) != kStackPoison)
    lucent::error("vsync", "the vblank handler chain ran off the bottom of its {}-byte stack at "
                           "0x{:08X} — it has been writing below it, and whatever lives there is "
                           "already corrupt. Raise kHandlerStackBytes.",
                  kHandlerStackBytes, kHandlerStackFloor);
  static const lucent::Channel ch_vsync{"vsync"};
  if (ch_vsync) {                       // guards the SCAN, not the print
    static uint32_t deepest = kHandlerStackTop;
    const uint32_t lw = handler_stack_low_water(c);
    if (lw < deepest) {
      deepest = lw;
      lucent::debug(ch_vsync, "vblank handler stack peak: {} bytes used (low water 0x{:08X} of "
                              "[0x{:08X},0x{:08X}))",
                    kHandlerStackTop - lw, lw, kHandlerStackFloor, kHandlerStackTop);
    }
  }
  return root != 0;
}

// The libetc vblank counter. Derived from the wait helper's own body: it compares
// `[0x800749E0] < a0` as its loop condition: lui 0x8007 (=0x80070000) + lw offset 18912 (=0x49E0).
constexpr uint32_t kVblankCounter = 0x800749E0u;

// The wait helper, 0x8005DD0C: wait(a0 = target count, a1 = timeout budget >> 15).
constexpr uint32_t kVblankWait = 0x8005DD0Cu;

// A single wait should never span more than a fraction of a second of vblanks. A far-future target
// means we misread the counter or the guest asked for something unreasonable; advancing to it
// blindly would spin out thousands of frames and look like a hang. Cap it, and SAY so — silently
// clamping is how a wrong timebase hides.
constexpr int kMaxVblanksPerWait = 300;   // ~5s at 60Hz

// ── `PSXPORT_DEBUG=pace` — the instrument that CHECKS `GameConfig::paceQuota` against a real run ──
//
// paceQuota declares how many vblanks ONE `gpu_pace_frame` call represents, and the framework sleeps
// exactly `quota/60 s` per call on the strength of that declaration (gpu_native.cpp
// gpu_pace_subframe). It is therefore a claim about THIS FILE's calling cadence, and a wrong claim
// does not fail — it silently multiplies the port's frame time and reads as "the port is slow".
// Spider-Man's paceQuota sat at 2 against a 1-vblank cadence for exactly that reason, and the port
// rendered at half rate while presenting normally. So measure the cadence rather than asserting it.
//
// The tallies are kept UNCONDITIONALLY, one increment per event, on the same lines as the events
// themselves — `lucent::debug` does not evaluate its arguments when the channel is off, so a counter
// bumped inside the log call would only count while someone was watching, which is the classic
// instrument that cannot report its own denominator.
//
// WHAT A NEGATIVE LOOKS LIKE HERE, by construction. `pace` lines ABSENT entirely means either the
// channel is off or this build never delivered a field — not "the port never paced". A `pace`
// line whose `pace` and `vbl` counters diverge means the port paced a different number of times
// than it advanced vblanks, which is the exact defect paceQuota encodes; equal counters with
// `vbl/s` well under 60 means the pacer is sleeping longer than one vblank per call (a quota too
// high, or the host missing the deadline) and the two are told apart by `quota` on the same line.
// `site=` names WHICH caller asked for the field — `vsync` (the guest reached a VSync wait) or
// `hostturn` (the guest is in a loop that calls no wait, so the host clock delivered it). A run
// whose `pace` lines are ALL site=vsync is a run in which the host turn never fired; that is the
// distinction the memory-card softlock turns on, so it is on every line rather than inferred.
// BLIND SPOTS: pace calls made from anywhere else. Bounded by grep — the only other callers in the
// framework are native_boot.cpp / native_stub.cpp (the native frame loop, which this port does not
// run: the guest owns its loop) and fps60.cpp (unreachable here — its eligibility needs
// RenderQueue::drawWorldQuad, which this repo never calls). `present` likewise counts only the
// presents THIS file makes; the framework's boot stub presents outside it.
//
// AND THIS INSTRUMENT DOES NOT MEASURE THE DRAW RATE — say so rather than let a reader assume the
// vblank rate is it. The guest's DrawOTag reaches the GPU through DMA2, and the framework drains the
// queue at the END OF THAT WALK (gpu_native.cpp `gpu_dma2_linked_list` -> rq.flush), so by the time
// this loop flushes, the queue is already consumed and this boundary cannot see a draw happen. The
// rate of NEW rendered frames is `rebuild_geom` from `PSXPORT_DEBUG=presentskip`, which is a
// framework counter over the composite decision; run the two channels together.
unsigned long g_pace_entries = 0;   // gpu_pace_frame calls made from this file
unsigned long g_pace_vbl     = 0;   // display fields this file delivered
unsigned long g_pace_present = 0;   // gpu_present calls made from this file
// Fields where THIS file was the queue's first consumer (n>0 and not yet consumed). Expected 0
// for this port, per the paragraph above — it is the instrument's own check that the sample point is
// downstream of the DMA2 flush, so a NON-zero value is the interesting answer and means a second
// producer is queueing outside the OT walk.
unsigned long g_pace_rq_unconsumed = 0;

// Wall clock on the same line as the counters, so the rates are derivable from the log alone
// without trusting a separate timestamp source to share this one's clock.
double pace_ms() {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  static double t0 = -1;
  double now = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
  if (t0 < 0) t0 = now;
  return now - t0;
}

// ── ONE DISPLAY FIELD, ONE DEFINITION ──────────────────────────────────────────────────────────────
//
// Everything the console's VBlank does, in the order it does it. There are TWO callers and they must
// not be two implementations of "a field happened":
//
//   * `vblank_wait` below — the guest reached a VSync wait and is asking for fields;
//   * `spyro_host_turn` — the guest is in a loop that waits for a field WITHOUT calling anything the
//     port owns, so nothing would ever ask. The framework's host clock says a field is due and the
//     port delivers it at the next recompiled-function entry or loop back-edge.
//
// WHY THE SECOND CALLER EXISTS AT ALL, measured rather than asserted. libmcrd's MemCardSync(mode=0)
// at 0x80067628 blocks in a bare spin (`generated/shard_4.c` L_80067684: `v0 = [0x80075B58]; beq v0,
// zero, back` — nothing else in the loop), and the SOLE writer of that flag is the card driver's
// completion callback 0x80067CD4, which the game installed as libetc VBlank callback slot 7. That
// spin calls no library function, so before this the port had no way to deliver the field the flag
// depends on and the run wedged with no frame ever presented again — the user's "SELECT MEMORY CARD
// softlock". The spin is not special: it is one member of a class (the card READ and WRITE op state
// machines have the same shape), so the fix is the framework's class-wide mechanism rather than an
// override on that one address. `rec_host_turn_register` is one line; see host_turn.cpp for why its
// arming interval is a hardware fact and not a tuned constant.
//
// NOT RE-ENTRANT, and that is the console's behaviour rather than a convenience. The BIOS runs the
// VBlank root handler with interrupts masked, so a second field cannot begin inside the first; the
// framework's event delivery refuses nested delivery for exactly this reason (hle.cpp `ev_depth`).
// The guard matters because a host turn is taken at a guest function entry, and this function runs
// guest code — without it a turn could open a field inside a field and re-enter the game's own
// handler halfway through its own non-atomic update.
bool g_in_field = false;

unsigned long g_field_refused = 0;   // fields a caller asked for while one was already in flight

bool deliver_field(Core* c, const char* site, bool needsPacing) {
  // See the header: a field cannot begin inside a field. A host turn is taken at a guest function
  // entry or loop back-edge, and the guest code this function runs contains plenty of both — the
  // card state machine's own retry loops among them — so without this the game's vblank handler
  // re-enters itself to unbounded depth. MEASURED, on the way to this line: the watchdog backtrace
  // from the memory-card wedge showed gen_func_8005E560 -> 0x80067CD4 -> 0x80068FC4 -> 0x800671F0 ->
  // rec_irq_poll -> rec_host_turn -> gen_func_8005E560 again, one full nesting deep.
  if (g_in_field) {
    ++g_field_refused;
    lucent::debug("pace", "field refused (site={}): one is already in flight — a vblank handler is "
                          "not re-entrant on hardware either. refused={} so far", site,
                  g_field_refused);
    return false;
  }
  g_in_field = true;
  // DRAIN THE RENDER QUEUE. The guest's DrawOTag walks its OT and QUEUES each prim
  // (gpu_dma2_linked_list -> gpu_gp0 -> rq.push), but nothing emits that queue to the renderer:
  // rq.flush() is only reached from the framework's native_boot / Engine::drawOTag path, which this
  // port never runs. The queue only resets lazily on the push AFTER it was consumed, so with no
  // consumer it grows without bound.
  //
  // That single omission produced BOTH of the port's symptoms. Nothing reached the VK renderer, so
  // every frame after the logo fade was BLACK; and the queue accumulated ~449 polys/frame until it
  // hit RQ_MAX 65536 about 146 drawing-frames later and the framework fail-fasted, which is the
  // abort at frame 3781 (issue 0015). psxport's own history records the identical bug with the
  // identical black-front-end symptom — see the comment in native_boot.cpp.
  //
  // Here is the right place for the same reason the event delivery below is: this wait IS the
  // port's per-frame boundary. Flush before present, so the frame being presented is the one the
  // guest just drew.
  // Read the queue BEFORE it is flushed, for the `pace` line: `n>0 && !consumed` would mean this
  // loop is the queue's FIRST consumer this frame. See g_pace_rq_unconsumed above.
  const int rq_n = c->game->rq.n, rq_unconsumed = (rq_n > 0 && !c->game->rq.consumed) ? 1 : 0;
  g_pace_rq_unconsumed += (unsigned)rq_unconsumed;
  c->game->rq.flush(c);
  // FILL THE PAD BUFFERS. Spyro's libpad (0x80069000-0x8006C000) fills them from SIO0 inside the
  // VBlank IRQ handler, and this runtime raises no IRQs — so on the port that state machine never
  // runs and the buffers keep the 0xFF "no controller" byte its init wrote (0x8006B100). The
  // decoder at 0x80053F00 then takes its no-pad arm every frame, which is why the only input the
  // game ever saw was the attract demo's recorded stream (C062). serviceFrame() does exactly what
  // the VBlank read would have: poll the host and write the standard packet into the buffers named
  // in GameConfig. It belongs here for the same reason the present and the event delivery do —
  // this wait IS the port's per-frame boundary — and it must run BEFORE the guest's decoder, which
  // it does: the guest reads input inside its own frame body, after returning from this wait.
  c->game->pad.serviceFrame();
  // …then run the vblank handler the game registered, which is what CONSUMES that packet. Order
  // matters and is the console's: SIO fills the buffer, then the VBlank callback decodes it.
  const bool guestOwnsCount = run_vblank_callback(c);
  // A COMPLETED FRAME is the only safe place to capture guest RAM: mid-frame the OT and packet pool
  // are half-built, which is the one state nobody wants to reason about. The guest still owns its
  // frame loop here, so the framework cannot know where that boundary is — this wait does.
  // PSXPORT_SNAP_AT / PSXPORT_SNAP_EVERY / kill -USR1 <pid>; see snapshot.h.
  snapshot_tick(c);
  // PUMP THE REPL. `PSXPORT_REPL=1` gives an interactive prompt on stdin — read/write guest memory,
  // press pad buttons, dump RAM, step N frames. It was UNREACHABLE in this port for a structural
  // reason, not a broken one: repl.read() is only pumped from the framework's native scheduler loop,
  // and that loop never runs here because the guest still owns its frame loop. Pumping it from this
  // frame boundary costs nothing and makes a live port inspectable instead of requiring a rebuild
  // per question.
  //
  // read() blocks until the operator types `run N`, then returns N — a frame budget. So hold the
  // budget across frames and only re-enter when it runs out; that is exactly the contract the
  // native loop uses. A negative return is quit.
  if (cfg_on("PSXPORT_REPL")) {
    static long budget = 0;
    static bool quit = false;
    if (!quit && budget <= 0) {
      while ((budget = c->game->repl.read(c, c->mem_r32(kVblankCounter))) == 0) { }
      if (budget < 0) { quit = true; lucent::info("repl", "quit — running free"); }
    }
    if (budget > 0) budget--;
  }
  // One vblank = one displayed frame. present() puts the guest's drawn frame on screen; pace()
  // holds real time to the frame interval so the game runs at its intended speed rather than
  // spinning as fast as the host can.
  gpu_present(c);
  ++g_pace_present;
  // ADVANCE THE AUDIO MIXER. Exactly one video field of SPU clocks per displayed frame, drained to
  // the sink. Nothing else in this port ever advanced it: main.cpp opens the audio sink with
  // `spu_audio.init()` and `spu_audio.frame()` was called NOWHERE, so the SPU mixed no samples and
  // the port was SILENT — every voice, not just CD audio. The mixer is also the only caller of
  // CDC_GetCDAudioSample, so any XA stream would arm and then decode nothing.
  //
  // Here for the same reason the present, the pad service and the event delivery above are: the
  // framework's native_step_frame ("tick + per-vblank audio + present + pace") NEVER RUNS in this
  // port because the guest still owns its frame loop, and this wait is the port's real per-frame
  // boundary. One call per displayed frame keeps audio on the same clock as the picture.
  //
  // Found in spider1, which had the identical omission (that port's silent intro), and confirmed
  // here by grep before writing this: zero call sites.
  c->game->spu_audio.frame();
  // A host turn is already released by the framework's monotonic field clock. Sleeping here would
  // make that clock overdue by another field; the next guest boundary would immediately take
  // another host turn, sleep again, and repeat forever. That feedback loop advances VBlank/audio/
  // present while starving the guest's frame logic — picture crawls while audio runs ahead.
  // Explicit guest/native frame boundaries are not clock-released, so they still own the sleep.
  if (needsPacing) {
    gpu_pace_frame(c);
    ++g_pace_entries;
    rec_host_turn_field_delivered(c);
  }
  // Deliver the per-frame IRQ-driven BIOS events. The framework normally does this in
  // native_step_frame — but that loop NEVER RUNS here, because the guest still owns its own frame
  // loop (game_hooks.cpp). This wait is the port's real per-frame point, so it is where the events
  // a game's TestEvent waits poll must be raised. Without it the classes in GameConfig are
  // configured but never delivered, and any such wait spins forever — which is exactly the stall
  // at func_8005CBB0 (it polls handle 0xF1000000, opened on class 0xF0000009).
  for (uint32_t cls : { c->cfg->irqEventClasses[0], c->cfg->irqEventClasses[1],
                        c->cfg->irqEventClasses[2] })
    if (cls) c->game->hle.deliverEvent(cls, 0xFFFFFFFFu);
  ++g_pace_vbl;
  lucent::debug("pace", "t={:.1f}ms vbl={} pace={} present={} rq_unconsumed={} | site={} quota={} "
                        "counter={} rq_n={} unconsumed={}",
                pace_ms(), g_pace_vbl, g_pace_entries, g_pace_present, g_pace_rq_unconsumed,
                site, c->cfg ? c->cfg->paceQuota : 0u, c->mem_r32(kVblankCounter), rq_n,
                rq_unconsumed);
  g_in_field = false;
  // THE PRODUCER DB'S FRAME BOUNDARY, and the only one this port has. Everything above this line is
  // here for the same reason (flush, present, pace, audio, events): the guest owns its frame loop, so
  // the framework's native_step_frame never runs and THIS field is the port's per-frame point. The DB
  // needs it for the one thing it could not otherwise have — an END. `PSXPORT_NATIVE_FRAMES=<n>`
  // makes this call end the run at frame n with the report written; uncapped it only counts. See
  // producer_run.h for why a periodic flush here would be wrong (appendClaims does not dedup).
  spyro_producer_run_frame(c);
  // The guest's root handler increments [0x800749E0] itself, so the caller must re-read it rather
  // than double-count. Only when it is absent (early boot, before libetc installs it) does the port
  // own the tick — which is what this return value says.
  return guestOwnsCount;
}

void vblank_wait(Core* c) {
  const int32_t target = (int32_t)c->r[4];
  int32_t cur = (int32_t)c->mem_r32(kVblankCounter);

  // A VSync WAIT reached from inside the vblank handler cannot be satisfied: the field it is waiting
  // for cannot begin until this one ends. That is a deadlock on the console too (the BIOS runs the
  // root handler with interrupts masked), so there is no behaviour to be faithful to — but the port
  // must not spin silently either. Say it, with the caller, and let the guest continue.
  if (g_in_field) {
    lucent::error("vsync", "VSync wait for {} entered from INSIDE the vblank handler (ra=0x{:08X}) — "
                           "the field it waits for cannot start until this one ends, which deadlocks "
                           "on hardware as well. Returning without waiting; the caller's timing is "
                           "wrong from here on, and this line is the only reason you will know.",
                  target, c->r[31]);
    if (cur < target) c->mem_w32(kVblankCounter, (uint32_t)target);
    return;
  }

  int advanced = 0;
  while (cur < target) {
    if (advanced >= kMaxVblanksPerWait) {
      lucent::warn("vsync", "wait target {} is {} vblanks ahead of the counter ({}) — clamped at {}. "
                            "Either the counter address is wrong or a caller asked for an implausible "
                            "wait; the timebase is suspect either way.",
                   target, target - cur, cur, kMaxVblanksPerWait);
      cur = target;   // satisfy the caller rather than hang, but the warning above is the real output
      break;
    }
    if (deliver_field(c, "vsync", true)) cur = (int32_t)c->mem_r32(kVblankCounter);
    else                           cur++;
    advanced++;
  }

  c->mem_w32(kVblankCounter, (uint32_t)cur);
  lucent::debug("vsync", "wait target={} -> counter={} (+{} frames)", target, cur, advanced);
}

// The host's turn: a field is due by the host clock and the guest has not asked for one. See
// deliver_field's header for the loop this exists for. Taking a turn is the framework's decision
// (host_turn.cpp owns the clock, the arming and the guest's critical sections); what a turn DOES is
// the port's, and for this port a turn is exactly one display field — the same one vblank_wait
// delivers, from the same function, so the two can never drift into two definitions of a field.
// `PSXPORT_DEBUG=hostturn` — WHERE the guest was when the host took a turn. A turn runs the game's
// own vblank handler at a point the game did not choose, so "which guest function, at what stack
// depth" is the first question any corruption blamed on it has to answer, and it cannot be recovered
// after the fact. The counter is bumped unconditionally so a run can report how many turns it took
// even with the channel off (`turns=` on the pace line's site=hostturn entries is the same number).
unsigned long g_host_turns = 0;

void spyro_host_turn(Core* c) {
  ++g_host_turns;
  lucent::debug("hostturn", "turn #{} at pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} gp=0x{:08X}",
                g_host_turns, c->pc, c->r[31], c->r[29], c->r[28]);
  deliver_field(c, "hostturn", false);
}

}  // namespace

// spyro_deliver_field — ONE display field, for a caller outside this file.
//
// The native render leg's frame tail owns Spyro's >= 2-field throttle (game/render/frame_env.cpp),
// and that throttle has to spend the SAME field this file defines — flush, pad, the guest's vblank
// callback, present — or the two legs run on two timebases that drift. So the field is exported
// rather than reimplemented; `deliver_field` itself stays file-local because its re-entrancy guard
// and its logging are this file's business.
bool spyro_deliver_field(Core* c, const char* site) { return deliver_field(c, site, true); }

void spyro_register_vsync(Game* g) {
  // Registration goes through PlatformHle::register_, which validates the address against
  // GameConfig::hle's windows — so this only installs if the libetc window actually covers it.
  g->platform_hle.register_(kVblankWait, vblank_wait);
  // VSyncCallback goes through the ordinary override registry, not platform_hle: it is not a
  // hardware-sync spin we are replacing, it is an observation point on a real library body that
  // still runs (the super-call in vsync_callback_set).
  psxport_recomp()->shard_set_override(0x8005DE58u, vsync_callback_set);
  // THE HOST CLOCK. Without this, a guest loop whose exit condition is only ever written by the
  // vblank callback can never terminate, because nothing between two guest instructions advances
  // time — the memory-card wait in deliver_field's header is one, and it is not the only member of
  // its class. The rate is read from the standard the GAME programmed into GP1(0x08)
  // (gpu_field_rate_millihz), not written here: field_rate.h exists so a field rate has exactly one
  // spelling. Registration runs before the guest boots, so this is the framework's default (NTSC)
  // unless the guest has already programmed the standard; Spyro is an NTSC disc and programs NTSC,
  // and if a PAL game ever needed this the fix is for GpuState to re-arm on a standard change, not a
  // literal here.
  rec_host_turn_register(&g->core, spyro_host_turn, gpu_field_rate_millihz(&g->core));
}
