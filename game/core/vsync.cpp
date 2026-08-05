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
bool run_vblank_callback(Core* c) {
  const uint32_t root = c->mem_r32(kRootHandlers);
  const uint32_t target = root ? root : g_vblank_cb;
  if (!target) return false;
  R3000 saved = *static_cast<R3000*>(c);
  rc0(c, target);
  *static_cast<R3000*>(c) = saved;
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

void vblank_wait(Core* c) {
  const int32_t target = (int32_t)c->r[4];
  int32_t cur = (int32_t)c->mem_r32(kVblankCounter);

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
        while ((budget = c->game->repl.read(c, (uint32_t)cur)) == 0) { }
        if (budget < 0) { quit = true; lucent::info("repl", "quit — running free"); }
      }
      if (budget > 0) budget--;
    }
    // One vblank = one displayed frame. present() puts the guest's drawn frame on screen; pace()
    // holds real time to the frame interval so the game runs at its intended speed rather than
    // spinning as fast as the host can.
    gpu_present(c);
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
    gpu_pace_frame(c);
    // Deliver the per-frame IRQ-driven BIOS events. The framework normally does this in
    // native_step_frame — but that loop NEVER RUNS here, because the guest still owns its own frame
    // loop (game_hooks.cpp). This wait is the port's real per-frame point, so it is where the events
    // a game's TestEvent waits poll must be raised. Without it the classes in GameConfig are
    // configured but never delivered, and any such wait spins forever — which is exactly the stall
    // at func_8005CBB0 (it polls handle 0xF1000000, opened on class 0xF0000009).
    for (uint32_t cls : { c->cfg->irqEventClasses[0], c->cfg->irqEventClasses[1],
                          c->cfg->irqEventClasses[2] })
      if (cls) c->game->hle.deliverEvent(cls, 0xFFFFFFFFu);
    // The guest's root handler increments [0x800749E0] itself, so re-read it rather than double-count.
    // Only when it is absent (early boot, before libetc installs it) does the port own the tick.
    if (guestOwnsCount) cur = (int32_t)c->mem_r32(kVblankCounter);
    else                cur++;
    advanced++;
  }

  c->mem_w32(kVblankCounter, (uint32_t)cur);
  lucent::debug("vsync", "wait target={} -> counter={} (+{} frames)", target, cur, advanced);
}

}  // namespace

void spyro_register_vsync(Game* g) {
  // Registration goes through PlatformHle::register_, which validates the address against
  // GameConfig::hle's windows — so this only installs if the libetc window actually covers it.
  g->platform_hle.register_(kVblankWait, vblank_wait);
  // VSyncCallback goes through the ordinary override registry, not platform_hle: it is not a
  // hardware-sync spin we are replacing, it is an observation point on a real library body that
  // still runs (the super-call in vsync_callback_set).
  psxport_recomp()->shard_set_override(0x8005DE58u, vsync_callback_set);
}
