// frame_loop.cpp — Spyro's per-frame loop, owned by the PORT instead of by the guest.
//
// WHY THIS FILE EXISTS. Every native-graphics step in this port needs one thing first: a place in
// PORT code that runs once per LOGIC FRAME, above the guest's renderers, where a native producer
// can be called instead of a recompiled one. Until now there was no such place.
// `Spyro1Runtime::bootInit` dispatched the guest's `main()` and never came back (proof below), so
// the framework's own frame loop (native_boot.cpp `native_step_frame`) never ran,
// `GameHooks::frameUpdate` / `GameHooks::drawOTag` were never called, and the only per-frame point
// the port had was the libetc vblank wait in vsync.cpp — which is a VBLANK boundary, not a frame
// boundary, and fires twice per drawn frame.
//
// THE GUEST'S main() IS A 15-INSTRUCTION SHELL, which is what makes owning it cheap. Disassembled
// at 0x80012204 (scratch/decomp/frameown.c + `disasm.py <dump> 0x80012204 0x800122A0`):
//
//     80012214  jal 0x8005B988      static-ctor table walk (empty: its count immediate is 0)
//     8001221C  jal 0x800127C0      boot init — CD loads, logo fades, display setup
//     8001222C  sb  zero, 0x604(gp) LOOP HEAD: close the input-latch window
//     80012230  jal 0x8003385C      per-frame UPDATE (stage-dispatched game logic)
//     80012238  lw  v0,   0x4fc(gp) vblanks elapsed since the last logic frame
//     8001223C  sb  s2,   0x604(gp) open the input-latch window (s2 == 1)
//     80012240  sw  v0,   0x468(gp) frame step = that count …
//     80012244..80012268                   … clamped to [2,4]
//     8001226C  lw  v0,   0x538(gp) render-suppress flag (0x8003385C zeroes it on entry)
//     80012270  sw  zero, 0x4fc(gp) restart the vblank count for the next frame
//     80012274  bnez v0, 0x8001222C suppressed -> straight back to the loop head
//     8001227C  jal 0x8001ED5C      per-frame RENDER + display + the >=2-vblank throttle
//     80012284  j   0x8001222C
//     8001228C..8001229C            epilogue — UNREACHABLE. Nothing branches to it.
//
// THE EPILOGUE IS UNREACHABLE, so `main()` never returns; that is the static half of the proof. The
// runtime half: `PSXPORT_SELFTEST=startgame` (whose whole job is to boot a core and then step it
// with `dc_step_frame`) hangs with a stack of
// `_start -> … -> dc_boot_init -> gen_func_80012204 -> gen_func_8001ED5C -> …` — dc_boot_init never
// returned, so its caller's stepping loop never began.
//
// SO THIS PORT CANNOT USE THE FRAMEWORK'S FRAME LOOP, and must not try to. Two independent reasons:
//   * REACHABILITY. `native_step_frame` is reached only from `game_main` (via native_crt0 <-
//     native_boot_run <- BootStub::run) and from `dc_step_frame`. `BootStub::run` has ZERO callers
//     in this repo — Spyro boots one executable with no SCEA stub (CLAUDE.md), so that whole spine
//     is dead code here. Tomba!2 is the port that calls it (`game->stub.run(path)` in its
//     main.cpp), which is why its native renderer hangs off `native_step_frame`.
//   * SHAPE. Even if it were reached, `native_step_frame`'s per-frame OT/packet-pool block assumes
//   ONE
//     `otBasePtr` global rewritten per frame. Spyro keeps per-parity pool pointers INSIDE its two
//     draw envs and selects by pointer (claim C073, re-confirmed on a gameplay snapshot), which is
//     why every field of that GameConfig group is deliberately 0 here.
// The loop therefore lives in GAME code, as a readable port of 0x80012204 — no framework change.
//
// WHAT THIS FILE DOES *NOT* YET TAKE OVER, and why saying so matters: PRESENT and PACE. Measured
// headless with `PSXPORT_DEBUG=pace` + `PSXPORT_FNTRACE`, on two different builds: 21551
// presents/vblanks against 11333 calls of 0x8001ED5C over 75 s, and 28218 against 15053 over 60 s
// unpaced — 1.86-1.90 vblanks per drawn frame either way. Spyro's logic frame is ~30 Hz
// (0x8001ED5C's tail spins until at least 2 vblanks have passed since the previous frame) while the
// display is 60 Hz. So moving present into THIS loop, one present per iteration, would halve the
// port's present rate. The vblank handler in vsync.cpp keeps present/pace/audio/events until the
// render driver's own wait is owned — step (2) of docs/re-frontier.md `frame.own-render-driver`.
//
// WHERE THE PICTURE COMES FROM IS NOT THIS FILE'S DECISION. The loop calls the render seam
// (game/render/render_frame.cpp `SpyroRenderer::drawFrame`) and that seam picks the leg from the
// framework's per-Core `RenderMode`: reference (`PSXPORT_RENDER_PSX=1`, the guest's own render
// driver) or native (the default, which today aborts on the first frame naming the scene it cannot
// draw — by design; see render.h).
//
// THE PORT OWNS THE LOOP, UNCONDITIONALLY. This file used to be bring-up behind
// `PSXPORT_SPYRO_FRAME_LOOP=1`, and the reason given was that the native render leg aborts on its
// first drawn frame — which is still true, but it is a reason to make the loop THE path, not to
// gate it: a port that cannot reach a drawn frame without an env flag is a port whose observable
// behavior is env-gated, which the framework forbids. The reference leg (`PSXPORT_RENDER_PSX=1`)
// still selects the guest's own renderer, so a run that wants the byte-exact picture asks for it
// the same way it always did; the loop no longer cares which leg.
#include "cfg.h" // cfg_on — the render-leg switch is a feature flag; the loop itself is not
#include "core.h"
#include "game.h"
#include "guest_call.h" // rc0 — run a guest function to its `jr ra`
#include "guest_gp.h"   // kGp — every gp-relative address below is derived from it
#include "render.h"     // SpyroRenderer — the render seam (game/render/)
#include "spyro_game.h"
#include <lucent/log.h>

namespace {

// ── The guest functions the loop calls ───────────────────────────────────────────────────────────
// Addresses read from the disassembly above; `tools/whatis.py <addr>` cross-references each. The
// render driver 0x8001ED5C is NOT here: it is the reference leg of the render seam and lives with
// it, in game/render/render.h.
constexpr uint32_t kStaticCtors = 0x8005B988u; // run-once fn-ptr table walk; its count is 0
constexpr uint32_t kBootInit = 0x800127C0u;    // CD loads + logo fades + display setup
constexpr uint32_t kFrameUpdate = 0x8003385Cu; // per-frame game logic, stage-dispatched

// ── The gp-relative loop block. gp = 0x80075264 (guest_gp.h, from crt0 at 0x8005B95C). ───────────
// Every literal below is kGp + the signed 16-bit displacement in the instruction it came from, so
// the arithmetic is checkable against the listing rather than being a magic address.

// gp+0x604 — the INPUT-LATCH WINDOW flag, and the reason the two writes below are load-bearing.
// The vblank callback 0x80053C68 (installed via VSyncCallback, decompiled in
// scratch/decomp/frameown.c) reads it: while it is 0 it only records the raw pad word, and while it
// is 1 it accumulates the pressed/released EDGES into 0x80077378 / 0x8007737C that the game logic
// reads. So the window must be CLOSED across the update and OPEN across the render+wait, exactly as
// the guest sequences it.
constexpr uint32_t kInputLatchOpen = kGp + 0x604u; // 0x80075868, a byte

// gp+0x4fc — vblanks elapsed since the last logic frame. INCREMENTED BY THE VBLANK CALLBACK (the
// last statement of 0x80053C68) and zeroed here. Confirmed live by `tools/whowrites.py 0x80075760`:
// the two innermost writers over a 110 s run are gen_func_80053C68 (20588 stores) and
// gen_func_80012204 (17771).
constexpr uint32_t kVblanksThisFrame = kGp + 0x4FCu; // 0x80075760

// gp+0x468 — the FRAME STEP the game logic multiplies its per-frame deltas by (0x8002A6FC,
// 0x800756BC and the 0x80075918 timer all consume it). Clamped to [2,4]: 2 because the render
// driver will not let a frame end in under 2 vblanks, 4 to bound a catch-up after a long CD load.
constexpr uint32_t kFrameStep = kGp + 0x468u; // 0x800756CC
constexpr int kFrameStepMin = 2;
constexpr int kFrameStepMax = 4;

// gp+0x538 — RENDER SUPPRESS. 0x8003385C zeroes it as its very first statement and some of its arms
// set it; when it is non-zero this frame draws nothing. Measured over one run: 11341 updates
// produced 11333 renders, so 8 frames were suppressed.
constexpr uint32_t kRenderSuppressed = kGp + 0x538u; // 0x8007579C

// ── A typed lens over the loop block, so the body below reads as the loop it is ──────────────────
class FrameState {
public:
  explicit FrameState(Core *c) : mC(c) {}

  // The two writes that bracket the update. See kInputLatchOpen for why the ORDER is behaviour.
  void closeInputLatchWindow() {
    mC->mem_w8(kInputLatchOpen, 0);
  }
  void openInputLatchWindow() {
    mC->mem_w8(kInputLatchOpen, 1);
  }

  int vblanksThisFrame() const {
    return (int32_t)mC->mem_r32(kVblanksThisFrame);
  }
  void restartVblankCount() {
    mC->mem_w32(kVblanksThisFrame, 0);
  }

  void setFrameStep(int n) {
    mC->mem_w32(kFrameStep, (uint32_t)n);
  }
  bool renderSuppressed() const {
    return mC->mem_r32(kRenderSuppressed) != 0;
  }

private:
  Core *mC;
};

// The loop itself — a readable port of 0x80012204. The guest sequence is preserved exactly,
// including the order of the two input-latch writes relative to the update and to the vblank-count
// read.
[[noreturn]] void run(Core *c) {
  FrameState fs(c);
  rc0(c, kStaticCtors);
  rc0(c, kBootInit);
  lucent::info("frameloop",
               "the PORT owns Spyro's frame loop (guest main 0x{:08X} is not dispatched)",
               0x80012204u);
  SpyroRenderer renderer(c);
  for (;;) {
    // THE LOGIC-FRAME COUNTER, advanced HERE because this loop is this port's logic frame.
    // `Timing::logicFrame` is the framework's per-Core "which logic frame is this", and its ONLY
    // writer is native_step_frame (runtime/recomp/native_boot.cpp:110) — a loop this port never
    // enters, so it stayed 0 for the entire run. That is not cosmetic: `census_frame()` stamps
    // every graphics-producer row with it, so the producer DB reported `frames 1 (f0..f0)` for a
    // producer that is CALLED on ~1282 consecutive frames and DRAWS on 697 of them — two different
    // questions, and this comment used to give only the first. #59's verification run
    // (scratch/logs/prod59_run3.log, cap 3000) counts 585 `emitted=0` + 14 `emitted=1` + 683
    // `emitted=2` = 1282 calls, so 14+683 =
    // **697** drawing frames and 1380 prims, and the row it produced reads `frames 697
    // (f585..f1282)`. Reproduced independently at the same cap: `frames 697 (f585..f1282)`, 1380
    // prims (scratch/logs/vfx_db3000.log). A FEW FRAMES OF SPREAD AT THE CAP BOUNDARY IS EXPECTED
    // AND IS NOT A DEFECT: the cap counts PRESENTED FIELDS while this counter counts LOGIC frames,
    // so where the run is cut lands wherever the loop happens to be. MEASURED SPREAD over four runs
    // at the same cap 3000 — 697 (f585..f1282)/1380 prims, 696 (f585..f1281)/1378, 695
    // (f585..f1280)/1376 — i.e. the START is f585 every time and only the END moves, exactly 2
    // prims per frame because this screen draws two sprites. So f585 IS the invariant to assert on
    // and the total is NOT; quote a span with its log, never a bare frame count. THE BAND IS NOT A
    // BOUND: a later run on a build against pin 726d10c9 (md5 303763bdb3658e6b0dd74f29a9da4c34,
    // scratch/logs/V_native3000.log) landed on `frames 694 (f585..f1279)` / 1374 prims — OUTSIDE
    // the 695..697 / 1376..1380 quoted above. The arithmetic invariant held exactly (585
    // non-drawing + 14 one-prim + 680 two-prim = 694 drawing frames, 1374 prims), which is the
    // thing to check. Anyone who writes 695..697 into an assertion has turned an observed range
    // into a fake bound; only `first_frame == 585` and the arithmetic are invariants. The counter's
    // documented tick is the LOGIC frame, not the present — and one iteration of this loop is
    // exactly that tick, which is why it is advanced here and not in producer_run.cpp (that hook
    // runs per PRESENTED FIELD, ~1.9 per logic frame, and would have re-created the s_frame defect
    // census_frame.h exists to fix). Same defect class as #58: framework state welded to a code
    // path a port with its own frame loop never reaches.
    ++c->game->timing.logicFrame;
    //
    // THE OTHER HALF OF THE SAME CONTRACT, and it was missing. The framework's own frame loop does
    // TWO things per logic frame, not one: `native_step_frame` sets `timing.logicFrame`
    // (native_boot.cpp:112) AND `native_boot_run` calls `otAttr.beginLogicFrame(f)`
    // (native_boot.cpp:384). The line above replicated only the first, so this port drove NEITHER
    // writer of OtAttr's span-table reset — `resetIfNewFrame` is reached from `beginLogicFrame` and
    // from nowhere else. Same root cause as the counter above (framework state whose only writer is
    // a loop this port never enters), so it is fixed in the same place rather than left as a second
    // latent copy of it.
    //
    // HONEST STATUS, because this must not be read as a fix for an observed symptom: it is DORMANT
    // TODAY and I proved why rather than assuming it. `trackStoreSlow` records a span only for
    // stores inside the packet pool (`if (!pool.known || k < pool.lo || k >= pool.hi) return;`,
    // ot_attr.cpp), and `pool_range` returns `known=false` because this game's
    // `packetPoolBase/Stride` are 0 (game_config.cpp:79-80, issue #56). So the span table takes
    // ZERO entries, there is nothing to go stale, and the reference leg's 100%-span-miss census is
    // fully explained by #56 alone — the missing reset is NOT a second cause of it, which is what I
    // checked before writing this line. It stops being dormant the moment #56 lands: a filled pool
    // would fill this table and, with no reset ever firing, saturate it at SPAN_CAP against a frame
    // stamp that never changes — which is EXACTLY the saturation failure `beginLogicFrame` was
    // written to remove (ot_attr.h: 60 logic frames advanced s_frame to 3, 140,153 overflows, every
    // later lookup matching a stale span with fn 0). Driving it now costs one compare per frame and
    // means #56's fix cannot land on top of a silent trap.
    c->rsub.otAttr.beginLogicFrame(c->game->timing.logicFrame);
    fs.closeInputLatchWindow();
    rc0(c, kFrameUpdate);
    const int elapsed = fs.vblanksThisFrame();
    fs.openInputLatchWindow();
    int step = elapsed;
    if (step < kFrameStepMin) {
      step = kFrameStepMin;
    }
    if (step > kFrameStepMax) {
      step = kFrameStepMax;
    }
    fs.setFrameStep(step);
    const bool suppressed = fs.renderSuppressed();
    fs.restartVblankCount();
    if (suppressed) {
      continue;
    }
    renderer.drawFrame();
  }
}

} // namespace

[[noreturn]] void spyro_frame_loop_run(Core *c) {
  SpyroRenderer::installModeFromConfig(c); // which leg of the render seam this run uses
  run(c);
}
