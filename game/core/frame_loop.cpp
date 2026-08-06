// frame_loop.cpp — Spyro's per-frame loop, owned by the PORT instead of by the guest.
//
// WHY THIS FILE EXISTS. Every native-graphics step in this port needs one thing first: a place in
// PORT code that runs once per LOGIC FRAME, above the guest's renderers, where a native producer can
// be called instead of a recompiled one. Until now there was no such place. `spyro_bootInit` dispatched
// the guest's `main()` and never came back (proof below), so the framework's own frame loop
// (native_boot.cpp `native_step_frame`) never ran, `GameHooks::frameUpdate` / `GameHooks::drawOTag`
// were never called, and the only per-frame point the port had was the libetc vblank wait in
// vsync.cpp — which is a VBLANK boundary, not a frame boundary, and fires twice per drawn frame.
//
// THE GUEST'S main() IS A 15-INSTRUCTION SHELL, which is what makes owning it cheap. Disassembled at
// 0x80012204 (scratch/decomp/frameown.c + `disasm.py <dump> 0x80012204 0x800122A0`):
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
// runtime half: `PSXPORT_SELFTEST=startgame` (whose whole job is to boot a core and then step it with
// `dc_step_frame`) hangs with a stack of
// `_start -> … -> dc_boot_init -> gen_func_80012204 -> gen_func_8001ED5C -> …` — dc_boot_init never
// returned, so its caller's stepping loop never began.
//
// SO THIS PORT CANNOT USE THE FRAMEWORK'S FRAME LOOP, and must not try to. Two independent reasons:
//   * REACHABILITY. `native_step_frame` is reached only from `game_main` (via native_crt0 <-
//     native_boot_run <- BootStub::run) and from `dc_step_frame`. `BootStub::run` has ZERO callers in
//     this repo — Spyro boots one executable with no SCEA stub (CLAUDE.md), so that whole spine is
//     dead code here. Tomba!2 is the port that calls it (`game->stub.run(path)` in its main.cpp), which
//     is why its native renderer hangs off `native_step_frame`.
//   * SHAPE. Even if it were reached, `native_step_frame`'s per-frame OT/packet-pool block assumes ONE
//     `otBasePtr` global rewritten per frame. Spyro keeps per-parity pool pointers INSIDE its two draw
//     envs and selects by pointer (claim C073, re-confirmed on a gameplay snapshot), which is why
//     every field of that GameConfig group is deliberately 0 here.
// The loop therefore lives in GAME code, as a readable port of 0x80012204 — no framework change.
//
// WHAT THIS FILE DOES *NOT* YET TAKE OVER, and why saying so matters: PRESENT and PACE. Measured
// headless with `PSXPORT_DEBUG=pace` + `PSXPORT_FNTRACE`, on two different builds: 21551
// presents/vblanks against 11333 calls of 0x8001ED5C over 75 s, and 28218 against 15053 over 60 s
// unpaced — 1.86-1.90 vblanks per drawn frame either way. Spyro's logic frame is ~30 Hz (0x8001ED5C's
// tail spins until at least 2 vblanks have passed since the previous frame) while the display is
// 60 Hz. So moving present into THIS loop, one present per iteration, would halve the port's present
// rate. The vblank handler in vsync.cpp keeps present/pace/audio/events until the render driver's own
// wait is owned — step (2) of docs/re-frontier.md `frame.own-render-driver`.
//
// DEFAULT OFF. `PSXPORT_SPYRO_FRAME_LOOP=1` arms the loop; `PSXPORT_SPYRO_NATIVE_RENDER=1` then
// selects the native render branch, whose only correct behaviour today is to ABORT naming the scene it
// was asked to draw and cannot. That abort IS the deliverable — it turns the porting backlog into a
// crash sequence in dependency order and makes it impossible to ship a layer that merely looks
// finished. It is opt-in for the same reason `PSXPORT_NATIVE_TERRAIN` is: a port that aborts on its
// first frame by default makes every other measurement in this repo impossible to take.
#include "core.h"
#include "game.h"
#include "cfg.h"          // cfg_on — both switches below are feature flags, not diagnostics
#include "guest_call.h"   // rc0 — run a guest function to its `jr ra`
#include "spyro_game.h"
#include <lucent/log.h>
#include <stdlib.h>       // abort

namespace {

// ── The guest functions the loop calls ───────────────────────────────────────────────────────────
// Addresses read from the disassembly above; `tools/whatis.py <addr>` cross-references each.
constexpr uint32_t kStaticCtors      = 0x8005B988u;  // run-once fn-ptr table walk; its count is 0
constexpr uint32_t kBootInit         = 0x800127C0u;  // CD loads + logo fades + display setup
constexpr uint32_t kFrameUpdate      = 0x8003385Cu;  // per-frame game logic, stage-dispatched
constexpr uint32_t kFrameRenderDrv   = 0x8001ED5Cu;  // per-frame render + display, stage-dispatched

// ── The gp-relative loop block. gp = 0x80075264 (game_config.cpp, from crt0 at 0x8005B95C). ──────
// Every literal below is gp + the signed 16-bit displacement in the instruction it came from, so the
// arithmetic is checkable against the listing rather than being a magic address.
constexpr uint32_t kGp = 0x80075264u;

// gp+0x604 — the INPUT-LATCH WINDOW flag, and the reason the two writes below are load-bearing.
// The vblank callback 0x80053C68 (installed via VSyncCallback, decompiled in scratch/decomp/frameown.c)
// reads it: while it is 0 it only records the raw pad word, and while it is 1 it accumulates the
// pressed/released EDGES into 0x80077378 / 0x8007737C that the game logic reads. So the window must be
// CLOSED across the update and OPEN across the render+wait, exactly as the guest sequences it.
constexpr uint32_t kInputLatchOpen   = kGp + 0x604u;  // 0x80075868, a byte

// gp+0x4fc — vblanks elapsed since the last logic frame. INCREMENTED BY THE VBLANK CALLBACK (the last
// statement of 0x80053C68) and zeroed here. Confirmed live by `tools/whowrites.py 0x80075760`: the two
// innermost writers over a 110 s run are gen_func_80053C68 (20588 stores) and gen_func_80012204 (17771).
constexpr uint32_t kVblanksThisFrame = kGp + 0x4FCu;  // 0x80075760

// gp+0x468 — the FRAME STEP the game logic multiplies its per-frame deltas by (0x8002A6FC, 0x800756BC
// and the 0x80075918 timer all consume it). Clamped to [2,4]: 2 because the render driver will not let
// a frame end in under 2 vblanks, 4 to bound a catch-up after a long CD load.
constexpr uint32_t kFrameStep        = kGp + 0x468u;  // 0x800756CC
constexpr int      kFrameStepMin     = 2;
constexpr int      kFrameStepMax     = 4;

// gp+0x538 — RENDER SUPPRESS. 0x8003385C zeroes it as its very first statement and some of its arms
// set it; when it is non-zero this frame draws nothing. Measured over one run: 11341 updates produced
// 11333 renders, so 8 frames were suppressed.
constexpr uint32_t kRenderSuppressed = kGp + 0x538u;  // 0x8007579C

// gp+0x574 — the STAGE SELECTOR. Both 0x8003385C and 0x8001ED5C dispatch on it, so it names WHICH game
// mode is running, and therefore which scene a native renderer would have to produce.
constexpr uint32_t kStageSelector    = kGp + 0x574u;  // 0x800757D8

// ── The stage arms of the render driver 0x8001ED5C ───────────────────────────────────────────────
// Transcribed from the linear if-chain at 0x8001EDF8-0x8001EF80 and spot-checked against the
// disassembly (the chain compares [0x800757D8] against 1,2,3,4,5,6,7,8,9,0xA…0xF in order). Two arms
// are themselves conditional and two more are indirect; all four are represented honestly rather than
// collapsed to a single address.
//
// NOTHING HERE IS NAMED BEYOND ITS ADDRESS unless this repo already named it. A stage whose role has
// not been reverse-engineered gets "(role not RE'd)" — an invented name would read as knowledge.
struct StageArm {
  uint32_t stage;
  uint32_t handler;      // 0 when the arm dispatches indirectly or picks between two handlers
  const char* what;
};
constexpr StageArm kStageArms[] = {
  {  0, 0x00000000u, "FIELD / world — the arm that runs during gameplay (C151, snap_15000). "
                     "Not one call: a 10-entry layer list, see kFieldLayers" },
  {  1, 0x8001A050u, "(role not RE'd)" },
  {  2, 0x8001A40Cu, "(role not RE'd)" },
  {  3, 0x8001A40Cu, "(role not RE'd)" },
  {  4, 0x8001CA38u, "(role not RE'd) — reaches EmitStaticActorMeshList 0x8004EBA8" },
  {  5, 0x8001CA38u, "(role not RE'd) — reaches EmitStaticActorMeshList 0x8004EBA8" },
  {  6, 0x8001A40Cu, "(role not RE'd)" },
  {  7, 0x00000000u, "INDIRECT — calls (*[0x8007567C])(), so the handler is data, not code" },
  {  8, 0x8001CFDCu, "(role not RE'd)" },
  {  9, 0x8001A050u, "(role not RE'd)" },
  { 10, 0x8001C694u, "(role not RE'd)" },
  { 11, 0x8001D718u, "(role not RE'd)" },
  { 12, 0x8001E24Cu, "(role not RE'd)" },
  { 13, 0x00000000u, "SPLIT on [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4" },
  { 14, 0x8001E9C8u, "(role not RE'd) — reaches RenderWorldChunks 0x800258F0" },
  { 15, 0x00000000u, "SPLIT on [0x80075704]<99 -> 0x8007BFD0, else 0x8001EB80 "
                     "(the latter reaches RasterizeSpritePrimQueue 0x80022A2C)" },
};
constexpr uint32_t kStageIndirectPtr13 = 0x80078D78u;  // the [..]==3 discriminator of stage 13
constexpr uint32_t kStageIndirectPtr15 = 0x80075704u;  // the [..]<99 discriminator of stage 15
constexpr uint32_t kStageFnPtr7        = 0x8007567Cu;  // stage 7's function pointer

// ── The FIELD (stage 0) layer list ───────────────────────────────────────────────────────────────
// The stage-0 arm of 0x8001ED5C, in order, from the decompile in scratch/decomp/frameloop.c. `gate`
// is the global the guest tests before making the call; 0 means unconditional. This list IS the
// native-renderer backlog for the field, in the order the guest draws it.
struct FieldLayer {
  uint32_t fn;
  uint32_t gate;        // 0 = unconditional
  bool gateNonZero;     // true: runs when [gate] != 0; false: runs when [gate] == 0
  const char* what;
};
constexpr FieldLayer kFieldLayers[] = {
  { 0x800521C0u, 0,            false, "(role not RE'd)" },
  { 0x80019300u, 0x80075690u,  false, "(role not RE'd) — runs when [0x80075690] == 0" },
  { 0x80018908u, 0x80075714u,  true,  "(role not RE'd) — runs when [0x80075714] != 0" },
  { 0x80019698u, 0,            false, "actor pass — reaches EmitActorDrawList 0x8001F798 and "
                                      "EmitSecondaryActorPrimitives 0x80020F34" },
  { 0x8002B9CCu, 0,            false, "(role not RE'd)" },
  { 0x80050BD0u, 0,            false, "(role not RE'd)" },
  { 0x800573C8u, 0,            false, "(role not RE'd)" },
  { 0x800190D4u, 0x80075918u,  true,  "(role not RE'd) — runs when [0x80075918] != 0, called with "
                                      "([0x80075918]<<3) in a1/a2/a3" },
  { 0x80018F30u, 0,            false, "(role not RE'd) — runs when [0x8007570C] != 0 OR "
                                      "[0x800756C0] != 0; reported as always-armed here because this "
                                      "classifier does not evaluate the OR" },
  { 0x800189F0u, 0,            false, "(role not RE'd)" },
};

// ── A typed lens over the loop block, so the body below reads as the loop it is ──────────────────
class FrameState {
public:
  explicit FrameState(Core* c) : mC(c) {}

  // The two writes that bracket the update. See kInputLatchOpen for why the ORDER is behaviour.
  void closeInputLatchWindow() { mC->mem_w8(kInputLatchOpen, 0); }
  void openInputLatchWindow()  { mC->mem_w8(kInputLatchOpen, 1); }

  int  vblanksThisFrame() const   { return (int32_t)mC->mem_r32(kVblanksThisFrame); }
  void restartVblankCount()       { mC->mem_w32(kVblanksThisFrame, 0); }

  void setFrameStep(int n)        { mC->mem_w32(kFrameStep, (uint32_t)n); }
  bool renderSuppressed() const   { return mC->mem_r32(kRenderSuppressed) != 0; }
  uint32_t stage() const          { return mC->mem_r32(kStageSelector); }

private:
  Core* mC;
};

// ── The scene a native renderer is being asked to produce ────────────────────────────────────────
struct Scene {
  uint32_t stage;
  const StageArm* arm;      // null when the stage selector is outside 0..15 (the guest draws nothing)
};

Scene classifyScene(Core* c) {
  FrameState fs(c);
  const uint32_t s = fs.stage();
  for (const StageArm& a : kStageArms)
    if (a.stage == s) return { s, &a };
  return { s, nullptr };
}

// The native render branch. It draws NOTHING and says exactly what it was asked to draw.
//
// A FALLBACK HERE WOULD BE THE WHOLE BUG: a branch that quietly dispatched the guest's renderer, or
// drew something plausible, would let a half-ported scene read as finished — and the reason this
// project keeps re-deriving render bugs is that a plausible picture is indistinguishable from a
// correct one. So the honest behaviour is to stop, print the scene identity and the exact backlog for
// it, and let the crash sequence be the work list.
[[noreturn]] void abortUnimplemented(Core* c, const Scene& sc) {
  lucent::error("frameloop", "NATIVE RENDER NOT IMPLEMENTED — stage selector [0x{:08X}] = {}",
                kStageSelector, sc.stage);
  if (!sc.arm) {
    lucent::error("frameloop", "  stage {} is outside 0..15: the guest's render driver 0x{:08X} falls "
                               "off its if-chain and draws nothing for it, so there is nothing to port "
                               "for this stage — but reaching it means the selector is a value this "
                               "port has not seen before. Investigate before adding an arm.",
                  sc.stage, kFrameRenderDrv);
    abort();
  }
  lucent::error("frameloop", "  arm: {}", sc.arm->what);
  if (sc.arm->handler)
    lucent::error("frameloop", "  the guest would have called 0x{:08X}", sc.arm->handler);
  if (sc.stage == 7)
    lucent::error("frameloop", "  the guest would have called (*[0x{:08X}]) = 0x{:08X}",
                  kStageFnPtr7, c->mem_r32(kStageFnPtr7));
  if (sc.stage == 13)
    lucent::error("frameloop", "  [0x{:08X}] = {} selects 0x8001E6B8 (==3) or 0x8007CEE4",
                  kStageIndirectPtr13, c->mem_r32(kStageIndirectPtr13));
  if (sc.stage == 15)
    lucent::error("frameloop", "  [0x{:08X}] = {} selects 0x8007BFD0 (<99) or 0x8001EB80",
                  kStageIndirectPtr15, c->mem_r32(kStageIndirectPtr15));
  if (sc.stage == 0) {
    lucent::error("frameloop", "  the FIELD backlog, in the guest's own draw order — ARMED means the "
                               "layer's gate is satisfied on THIS frame, i.e. it is missing from the "
                               "picture right now:");
    for (const FieldLayer& L : kFieldLayers) {
      const bool armed = L.gate == 0 || ((c->mem_r32(L.gate) != 0) == L.gateNonZero);
      lucent::error("frameloop", "    [{}] 0x{:08X}  {}", armed ? "ARMED" : "  -  ", L.fn, L.what);
    }
  }
  lucent::error("frameloop", "  no fallback is installed on purpose: a native branch that drew "
                             "something plausible would make this gap invisible. Port the layers "
                             "above, or run without PSXPORT_SPYRO_NATIVE_RENDER.");
  abort();
}

bool g_nativeRender = false;

// ONE per-frame render step, with the two branches the port needs to compare.
//
// This is the seam Tomba!2 reaches through `GameHooks::drawOTag`. Spyro's `drawOTag` hook stays NULL
// and this function stands in its place, because that hook's ONLY call site is `native_step_frame`,
// which is unreachable in this port (see the header). Filling the hook would look like wiring and
// connect to nothing.
void renderFrame(Core* c) {
  if (!g_nativeRender) {
    rc0(c, kFrameRenderDrv);   // psx_render reference: the guest's own render driver, unmodified
    return;
  }
  abortUnimplemented(c, classifyScene(c));
}

// The loop itself — a readable port of 0x80012204. The guest sequence is preserved exactly, including
// the order of the two input-latch writes relative to the update and to the vblank-count read.
[[noreturn]] void run(Core* c) {
  FrameState fs(c);
  rc0(c, kStaticCtors);
  rc0(c, kBootInit);
  lucent::info("frameloop", "the PORT owns Spyro's frame loop (guest main 0x{:08X} is not dispatched); "
                            "render branch = {}",
               0x80012204u, g_nativeRender ? "NATIVE (aborts on the first frame, by design)"
                                           : "psx_render reference (the guest's 0x8001ED5C)");
  for (;;) {
    fs.closeInputLatchWindow();
    rc0(c, kFrameUpdate);
    const int elapsed = fs.vblanksThisFrame();
    fs.openInputLatchWindow();
    int step = elapsed;
    if (step < kFrameStepMin) step = kFrameStepMin;
    if (step > kFrameStepMax) step = kFrameStepMax;
    fs.setFrameStep(step);
    const bool suppressed = fs.renderSuppressed();
    fs.restartVblankCount();
    if (suppressed) continue;
    renderFrame(c);
  }
}

}  // namespace

bool spyro_frame_loop_enabled() {
  return cfg_on("PSXPORT_SPYRO_FRAME_LOOP") != 0;
}

[[noreturn]] void spyro_frame_loop_run(Core* c) {
  g_nativeRender = cfg_on("PSXPORT_SPYRO_NATIVE_RENDER") != 0;
  run(c);
}
