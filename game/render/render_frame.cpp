// render_frame.cpp — ONE frame's picture: the reference OT walk, or the native producers.
//
// This is Spyro's equivalent of Tomba!2's `Engine::drawOTag`, and it exists for the same reason: a
// single place, in PORT code, where "where does the picture come from" is decided once per frame.
// Spyro's `GameHooks::drawOTag` stays NULL — that hook's only call site is the framework's
// `native_step_frame`, which is unreachable in this port (C158) — so the frame loop calls this
// directly instead. Filling the hook would look like wiring and connect to nothing.
#include "cfg.h" // cfg_on — PSXPORT_RENDER_PSX is a feature flag, not a diagnostic
#include "core.h"
#include "fps60.h"     // checked access to Spyro 1's title-owned temporal presentation product
#include "frame_env.h" // nativeFrameBegin/End — the frame the native producers draw into
#include "fx_actor_draw.h"
#include "fx_paired_actor.h"
#include "fx_world_draw.h"
#include "game.h"       // Game::rq — the render queue the native producers emit into
#include "guest_call.h" // rc0 — run a guest function to its `jr ra`
#include "presentation_owner.h"
#include "render.h"
#include "spyro_game.h"
#include "stage13_scene_recipe.h"
#include <lucent/log.h>
#include <stdlib.h> // abort

void spyro_fps60_commit_field_delivered(Core *c);

namespace {
constexpr uint32_t kCamera = 0x80076dd0u;
}

// WHAT THE NATIVE LEG MEANS IN THIS PORT — one log line, no configuration. The render path itself
// is the framework's (installed in dc_boot_init); this only says what choosing `native` implies
// HERE.
void SpyroRenderer::installModeFromConfig(Core *c) {
  // NOTHING TO PARSE HERE ANY MORE. This used to re-read PSXPORT_RENDER_PSX itself, and its own
  // comment named the reason as a framework wart — "config parsing that belongs at Core setup lives
  // inside one particular boot spine". That is fixed upstream: the framework installs the render
  // path in `dc_boot_init` (psxport runtime/recomp/render_path.cpp + native_boot.cpp), a chokepoint
  // THIS port DOES go through — so the path is already correct here, including on a run that never
  // enables this frame loop. What is left is the one thing the framework cannot know. What the
  // NATIVE path means in THIS port specifically — which the framework cannot know. Stated
  // precisely, because the coarse version ("it aborts on anything not native") is not what the code
  // does: it aborts per STAGE (no producer registered) and when any stage-13 producer declines its
  // complete atomic recipe.
  if (!c->rsub.mode.psxRender()) {
    lucent::info(
        "render",
        "native path: stage {} front-end sprites, actors, world, and cyclorama. Aborts on a stage "
        "with no producer or when a complete stage-13 recipe is refused. "
        "PSXPORT_RENDER_PATH=gte for the reference picture (guest driver 0x8001ED5C).",
        (int)kStageFrontEnd);
  }
}

// THE REFERENCE PATH — permanent, not scaffolding. It is the byte-exact PSX picture: the only thing
// to compare a native producer against, and the way to DRIVE INTO a scene whose producer is not
// built yet. Nothing about it changes as producers land.
//
// The OT walk is inside it rather than beside it: the guest's driver ends in its own DrawOTag,
// which reaches the GPU through DMA2, and the framework walks the ordering table there
// (gpu_native.cpp `GpuState::gpu_dma2_linked_list`).
//
// NO rq.flush() HERE, deliberately, and this is where this port's shape differs from Tomba!2's.
// That DMA2 walk ALREADY flushes at its end (gpu_native.cpp, "FLUSH. The walk above ENUMERATES the
// guest's prims and QUEUES them"), so a flush on this line would re-emit an already-consumed queue
// — the queue only resets on the first push AFTER a flush, so flushing twice submits every prim
// twice. Measured on this build, `PSXPORT_DEBUG=rqflush,pace`, 15 s headless boot: 7936 flushes of
// which 5527 (69.6%) are flagged `reemit=1`, and `rq_unconsumed=0` over 3990 vblanks — i.e. the
// per-vblank flush in vsync.cpp was never once the queue's FIRST consumer, so every one of those
// re-emits is a duplicate submission. That is a real defect and it predates this seam
// (docs/issues/0053); the fix belongs in vsync.cpp, not in a second copy of the same mistake here.
void SpyroRenderer::referenceOtWalk() const {
  rc0(mC, kFrameRenderDrv);
}

// THE NATIVE PICTURE. One branch per stage that has a producer; every other stage ends in the abort
// below, and that abort is a DELIVERABLE rather than a gap being papered over. A branch that
// quietly dispatched the guest's renderer, or drew something plausible, would let a half-ported
// scene read as finished — and the reason this project keeps re-deriving render bugs is that a
// plausible picture is indistinguishable from a correct one. Stopping with the scene identity
// printed turns the porting backlog into a crash sequence in dependency order.
//
// STAGE 13 is composed from four native display owners in the guest's authored order: front-end
// sprites, actors, RenderWorldChunks, then the cyclorama. Each refuses before partial submission
// when its semantic input cannot be represented.
void SpyroRenderer::renderScene(const Scene &sc) const {
  if (sc.stage != kStageFrontEnd) {
    abortUnimplemented(sc, "no producer is registered for this stage");
  }
  const int32_t ofsX = mC->mem_r16s(mEnv + 8u), ofsY = mC->mem_r16s(mEnv + 10u);
  const int32_t cx = mC->mem_r16s(mEnv + 0u), cy = mC->mem_r16s(mEnv + 2u);
  const int32_t cw = mC->mem_r16s(mEnv + 4u), ch = mC->mem_r16s(mEnv + 6u);
  const uint32_t titleMode = mC->mem_r32(0x80078D78u);
  if (!spyro::stage13_scene_recipe::hasSharedBackdrop(titleMode)) {
    if (!stage13Mode3Render()) {
      abortUnimplemented(sc, "mode 3 also armed paired-actor renderer 0x80023AC4");
    }
    return;
  }
  if (!titleMenuRender(ofsX, ofsY, cx, cy, cx + cw - 1, cy + ch - 1)) {
    abortUnimplemented(sc, "the stage-13 producer declined this frame's menu mode");
  }
  if (!spyro_actor_submit(mC)) {
    abortUnimplemented(sc, "actor producer 0x8001F798 refused its atomic recipe");
  }
  const auto worldInvocation = spyro::stage13_scene_recipe::sharedBackdropInvocation();
  spyro::stage13_scene_recipe::apply(mC, worldInvocation);
  if (!spyro_world_submit(mC, worldInvocation.worldSelection)) {
    abortUnimplemented(sc, "world producer 0x800258F0 refused its atomic recipe");
  }
  if (!spyro_terrain_submit(mC, -1, kCamera + 0x14u, kCamera)) {
    abortUnimplemented(sc, "cyclorama producer 0x8004EBA8 refused its atomic recipe");
  }
}

[[noreturn]] void SpyroRenderer::abortUnimplemented(const Scene &sc, const char *why) const {
  lucent::error(
      "render", "NATIVE RENDER NOT IMPLEMENTED — stage selector = {} ({})", sc.stage, why);
  reportBacklog(sc);
  lucent::error("render",
                "  no fallback is installed on purpose: a native branch that drew "
                "something plausible would make this gap invisible. Port the scene above, "
                "or run with PSXPORT_RENDER_PSX=1 for the reference path.");
  abort();
}

// ONE frame's picture.
void SpyroRenderer::drawFrame() {
  const Scene sc = classifyScene();
  auto &paired = spyro_paired_actor_state(mC);
  Fps60 &temporal = fps60(*mC->game);
  const bool pairedState = mC->mem_r32(0x80078D7Cu) == 2u;
  spyro_paired_actor_frame_begin(paired, pairedState, mC->rsub.mode.psxRender(), temporal.active());
  temporal.mTier1EligibleCur = false;
  // `PSXPORT_DEBUG=scene`: what the classifier saw, EVERY drawn frame, on BOTH legs — the
  // denominator is the drawn-frame count, and an unnamed stage prints as loudly as a named one. It
  // is how "which scenes does a real run actually reach" gets answered with data rather than from
  // the stage table, and it works on the reference leg precisely because that leg is how you drive
  // INTO a scene whose producer does not exist yet.
  lucent::debug("scene",
                "stage={} leg={} arm={}",
                sc.stage,
                mC->rsub.mode.psxRender() ? "psx_render" : "native",
                sc.arm ? sc.arm->what : "(outside 0..15 — the guest draws nothing)");
  if (mC->rsub.mode.psxRender()) {
    // The guest driver may present from inside referenceOtWalk's VSync before this function gets
    // control back. Publish ownership first; the runtime policy must describe that inner present.
    spyro_presentation_owner(*mC).beginGuestFrame();
    const bool pairedOracle = sc.stage == kStageFrontEnd && mC->mem_r32(0x80078D7Cu) == 2u &&
                              (cfg_str("PSXPORT_PAIREDPOSE_ORACLE") != nullptr ||
                               cfg_str("PSXPORT_PAIRED_TRANSFORM_ORACLE") != nullptr ||
                               cfg_str("PSXPORT_PAIRED_FLOAT_XY_ORACLE") != nullptr);
    if (pairedOracle) {
      spyro_paired_actor_oracle_arm(mC);
    }
    referenceOtWalk();
    // The reference leg's guest driver (0x8001ED5C) ends in its own VSync wait, which presents via
    // deliver_field -> gpu_present — but that present reads the VK GEOMETRY BATCH, which flush
    // no longer fills: since the framework's ONE-PATH change, flush() CAPTURES into Fps60::mNCur
    // and never runs emitQueue, so the batch stays empty and the guest's present shows nothing
    // (rebuild_geom=0 forever). frame_commit -> present_vk is the ONLY emitter of the captured
    // queue, so the reference leg must go through it, exactly as Tomba2 does (game_tomba2.cpp:135 —
    // "frame_commit OWNS presentation in both configs"). The guest's own empty present is harmless;
    // frame_commit's present_vk is the one that actually draws. This also drains the capture, so no
    // reset_capture is needed (it was a band-aid that discarded the picture instead of emitting
    // it).
    temporal.frame_commit(mC, 1);
    spyro_fps60_commit_field_delivered(mC);
    if (pairedOracle && !spyro_paired_actor_oracle_finish(mC)) {
      abort();
    }
    if (!spyro_paired_actor_frame_finish(paired, true, false)) {
      abort();
    }
    return;
  }
  // Boot upload-only screens intentionally leave the default at guest VRAM. Reaching this explicit
  // native frame seam is the first point where the whole picture is known to come from producers.
  spyro_presentation_owner(*mC).beginNativeFrame();
  // THE FRAME THE PRODUCERS DRAW INTO. On the reference leg the guest's driver flips the draw env
  // and programs the GPU from it; on this leg nothing does, so the producers would emit into the
  // buffer that is NOT on screen and read as broken. game/render/frame_env.cpp owns that — it is
  // re-frontier `frame.own-render-driver` parts (1) and (2), written from the game's own DRAWENV.
  mEnv = nativeFrameBegin(mC);
  renderScene(sc);
  const bool expectPaired = sc.stage == kStageFrontEnd && mC->mem_r32(0x80078D7Cu) == 2u;
  if (!spyro_paired_actor_frame_finish(paired, false, expectPaired)) {
    abort();
  }
  bool pairedWorld = false, foreignWorld = false;
  for (int i = 0; i < mC->game->rq.n; ++i) {
    const RqItem &it = mC->game->rq.items[i];
    const bool tier1Owned = (it.layer == RQ_BACKGROUND && it.dbg_node == kBackdropDbgNode) ||
                            (it.layer == RQ_WORLD && it.has_xyf);
    if (!tier1Owned) {
      continue;
    }
    if (it.layer == RQ_WORLD && it.has_xyf && it.painter_object == 0x80023AC4u) {
      pairedWorld = true;
    } else {
      foreignWorld = true;
    }
  }
  temporal.mTier1EligibleCur = paired.endpoints_compatible && pairedWorld && !foreignWorld &&
                               spyro_paired_actor_fps60_eligible(paired);
  // THE ONE PLACE NATIVE PRIMS REACH THE RENDERER. Producers push into the render queue as they
  // draw; nothing is on screen until the queue is emitted, and this is that emit. Unreachable while
  // every scene aborts above — which is exactly why it is written now, next to the branch that will
  // reach it, rather than remembered later when the first producer renders nothing and the day goes
  // into finding out why.
  mC->game->rq.flush(mC);
  // …and show the buffer this env names. The guest's own tail is PutDispEnv(activeEnv + 0x5C); see
  // frame_env.cpp for why that displays the PREVIOUS iteration's buffer and why that is correct.
  //
  // THE NATIVE LEG PRESENTS ONLY THROUGH frame_commit — fps60CommitPending=true defers the present
  // and pace in deliver_field to it (spyro_deliver_field: !fps60CommitPending gates both). This is
  // the Tomba2-aligned shape: "frame_commit OWNS presentation in both configs"
  // (game_tomba2.cpp:130). The OLD code gated frame_commit behind fps60.active() and let
  // deliver_field present when fps60 was off — which meant the framework's flush CAPTURE
  // (rq_capture, unconditional since the ONE PATH change) was never drained by presentRotate, so
  // fps60=0 accumulated every flush and overflow-aborted. That is fixed by making frame_commit the
  // single fence for both configs.
  nativeFrameEnd(mC, mEnv, true);
  // THE PER-LOGIC-FRAME FENCE. flush() CAPTURES into Fps60::mNCur in both configs and frame_commit
  // (present_vk -> presentRotate -> mNCur = 0) is the ONLY drain. guestFields=1: one whole-field
  // pace for the ordinary frame; present_vk inserts the extra lerped frame only when fps60 is
  // active (extraFrame = active() && mHavePrev).
  temporal.frame_commit(mC, 1);
  spyro_fps60_commit_field_delivered(mC);
}
