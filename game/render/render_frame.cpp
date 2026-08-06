// render_frame.cpp — ONE frame's picture: the reference OT walk, or the native producers.
//
// This is Spyro's equivalent of Tomba!2's `Engine::drawOTag`, and it exists for the same reason: a
// single place, in PORT code, where "where does the picture come from" is decided once per frame.
// Spyro's `GameHooks::drawOTag` stays NULL — that hook's only call site is the framework's
// `native_step_frame`, which is unreachable in this port (C158) — so the frame loop calls this
// directly instead. Filling the hook would look like wiring and connect to nothing.
#include "render.h"
#include "core.h"
#include "game.h"         // Game::rq — the render queue the native producers emit into
#include "cfg.h"          // cfg_on — PSXPORT_RENDER_PSX is a feature flag, not a diagnostic
#include "guest_call.h"   // rc0 — run a guest function to its `jr ra`
#include <lucent/log.h>
#include <stdlib.h>       // abort

// PSXPORT_RENDER_PSX -> the framework's per-Core RenderMode.
//
// WHY THE GAME DOES THIS. The framework parses that flag in `native_boot_run` (native_boot.cpp), and
// this port never calls it: spyro boots through `main()` -> `dc_boot_init` -> the bootInit hook, a
// path `native_boot_run` is not on. So the flag would be silently inert here and the mode would be
// stuck at its default. This is the flag's own parse, landing in the framework's own per-Core
// variable — not a second switch with Spyro's name on it. (The duplication is a framework wart:
// config parsing that belongs at Core setup lives inside one particular boot spine. Worth fixing
// upstream; out of scope for a game-side step.)
void SpyroRenderer::installModeFromConfig(Core* c) {
  const bool psx = cfg_on("PSXPORT_RENDER_PSX") != 0;
  c->rsub.mode.setPsxRender(psx);
  lucent::info("render", "render path = {}",
               psx ? "psx_render REFERENCE (the guest's render driver 0x8001ED5C)"
                   : "NATIVE (aborts on the first frame naming the scene, by design — "
                     "PSXPORT_RENDER_PSX=1 for the reference path)");
}

// THE REFERENCE PATH — permanent, not scaffolding. It is the byte-exact PSX picture: the only thing
// to compare a native producer against, and the way to DRIVE INTO a scene whose producer is not
// built yet. Nothing about it changes as producers land.
//
// The OT walk is inside it rather than beside it: the guest's driver ends in its own DrawOTag, which
// reaches the GPU through DMA2, and the framework walks the ordering table there
// (gpu_native.cpp `GpuState::gpu_dma2_linked_list`).
//
// NO rq.flush() HERE, deliberately, and this is where this port's shape differs from Tomba!2's. That
// DMA2 walk ALREADY flushes at its end (gpu_native.cpp, "FLUSH. The walk above ENUMERATES the guest's
// prims and QUEUES them"), so a flush on this line would re-emit an already-consumed queue — the
// queue only resets on the first push AFTER a flush, so flushing twice submits every prim twice.
// Measured on this build, `PSXPORT_DEBUG=rqflush,pace`, 15 s headless boot: 7936 flushes of which
// 5527 (69.6%) are flagged `reemit=1`, and `rq_unconsumed=0` over 3990 vblanks — i.e. the per-vblank
// flush in vsync.cpp was never once the queue's FIRST consumer, so every one of those re-emits is a
// duplicate submission. That is a real defect and it predates this seam (docs/issues/0053); the fix
// belongs in vsync.cpp, not in a second copy of the same mistake here.
void SpyroRenderer::referenceOtWalk() const {
  rc0(mC, kFrameRenderDrv);
}

// THE NATIVE PICTURE. Today no scene has a producer, so every scene ends in the abort below.
//
// THAT ABORT IS THE DELIVERABLE, not a gap being papered over. A branch that quietly dispatched the
// guest's renderer, or drew something plausible, would let a half-ported scene read as finished — and
// the reason this project keeps re-deriving render bugs is that a plausible picture is
// indistinguishable from a correct one. Stopping with the scene identity printed turns the porting
// backlog into a crash sequence in dependency order.
//
// When the first producer is built it dispatches HERE, on `sc` — one branch per RE'd stage, each
// drawing from the game's own state into the render queue, each still ending at the same rq.flush in
// drawFrame(). Stages with no producer keep falling through to the abort.
void SpyroRenderer::renderScene(const Scene& sc) const {
  abortUnimplemented(sc);
}

[[noreturn]] void SpyroRenderer::abortUnimplemented(const Scene& sc) const {
  lucent::error("render", "NATIVE RENDER NOT IMPLEMENTED — stage selector = {}", sc.stage);
  reportBacklog(sc);
  lucent::error("render", "  no fallback is installed on purpose: a native branch that drew "
                          "something plausible would make this gap invisible. Port the scene above, "
                          "or run with PSXPORT_RENDER_PSX=1 for the reference path.");
  abort();
}

// ONE frame's picture.
void SpyroRenderer::drawFrame() const {
  const Scene sc = classifyScene();
  // `PSXPORT_DEBUG=scene`: what the classifier saw, EVERY drawn frame, on BOTH legs — the denominator
  // is the drawn-frame count, and an unnamed stage prints as loudly as a named one. It is how "which
  // scenes does a real run actually reach" gets answered with data rather than from the stage table,
  // and it works on the reference leg precisely because that leg is how you drive INTO a scene whose
  // producer does not exist yet.
  lucent::debug("scene", "stage={} leg={} arm={}", sc.stage,
                mC->rsub.mode.psxRender() ? "psx_render" : "native",
                sc.arm ? sc.arm->what : "(outside 0..15 — the guest draws nothing)");
  if (mC->rsub.mode.psxRender()) { referenceOtWalk(); return; }
  renderScene(sc);
  // THE ONE PLACE NATIVE PRIMS REACH THE RENDERER. Producers push into the render queue as they draw;
  // nothing is on screen until the queue is emitted, and this is that emit. Unreachable while every
  // scene aborts above — which is exactly why it is written now, next to the branch that will reach
  // it, rather than remembered later when the first producer renders nothing and the day goes into
  // finding out why.
  mC->game->rq.flush(mC);
}
