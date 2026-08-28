// render_frame.cpp — ONE frame's picture: the reference OT walk, or the native producers.
//
// This is Spyro's equivalent of Tomba!2's `Engine::drawOTag`, and it exists for the same reason: a
// single place, in PORT code, where "where does the picture come from" is decided once per frame.
// Spyro's `GameHooks::drawOTag` stays NULL: the title-owned FrameDriver calls this seam directly.
// Filling the legacy hook would create a second route to the same picture owner.
#include "cfg.h" // cfg_on — PSXPORT_RENDER_PSX is a feature flag, not a diagnostic
#include "core.h"
#include "cutscene_scene_recipe.h"
#include "fps60.h"     // checked access to Spyro 1's title-owned temporal presentation product
#include "frame_env.h" // nativeFrameBegin/End — the frame the native producers draw into
#include "fx_actor_draw.h"
#include "fx_paired_actor.h"
#include "fx_screen_fade.h"
#include "fx_world_draw.h"
#include "game.h"       // Game::rq — the render queue the native producers emit into
#include "gpu_vk.h"     // measured native/wide engine extents for the product-path announcement
#include "guest_call.h" // rc0 — run a guest function to its `jr ra`
#include "overlay_router.h"
#include "presentation_owner.h"
#include "render.h"
#include "screen_fade_recipe.h"
#include "spyro1_field_scheduler.h"
#include "spyro_game.h"
#include "stage13_scene_recipe.h"
#include <lucent/log.h>
#include <stdlib.h> // abort

namespace {
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kStageSelector = 0x800757D8u;
constexpr uint32_t kStageSubstate = 0x80078D78u;
constexpr uint32_t kStageSubSubstate = 0x80078D7Cu;
constexpr uint32_t kStateSwitch = 0x8007579Cu;
constexpr uint32_t kLoadStage = 0x80075864u;

bool pairedActorScene(Core *core, const Scene &scene) {
  return scene.stage == kStageFrontEnd && core->mem_r32(0x80078D78u) == 3u &&
         core->mem_r32(0x80078D7Cu) == 2u;
}
} // namespace

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
    lucent::info("render",
                 "native path: stage {} front-end and stage {} cutscene recipes. Aborts on a stage "
                 "with no producer or when a complete native recipe is refused. "
                 "PSXPORT_RENDER_PATH=gte for the reference picture (guest driver 0x8001ED5C).",
                 (int)kStageFrontEnd,
                 (int)kStageCutscene);
  }
}

// THE RETAINED REFERENCE BODY — the byte-exact A/B oracle for native producers. It is not currently
// a runnable player path: every reached retail render arm owns a VSync-based display tail, and the
// mandatory guest-VSync trap stops it until those diagnostic tails are split from scene production.
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
// former field-boundary flush was never once the queue's FIRST consumer, so every one of those
// re-emits was a duplicate submission. Issue 0053 removed that second consumer; the title
// FieldScheduler now flushes only when its request owns an immediate present.
void SpyroRenderer::referenceOtWalk() const {
  // This deliberately reaches the fatal VSync trap today. Do not add a success override here;
  // preserve the generated body and split the measured display tail when the diagnostic leg is
  // made runnable again.
  rc0(mC, kFrameRenderDrv);
}

// THE NATIVE PICTURE. One branch per stage that has a producer; every other stage ends in the abort
// below, and that abort is a DELIVERABLE rather than a gap being papered over. A branch that
// quietly dispatched the guest's renderer, or drew something plausible, would let a half-ported
// scene read as finished — and the reason this project keeps re-deriving render bugs is that a
// plausible picture is indistinguishable from a correct one. Stopping with the scene identity
// printed turns the porting backlog into a crash sequence in dependency order.
//
void SpyroRenderer::prepareScene(const Scene &sc) const {
  if (sc.stage == kStageCutscene) {
    const auto state = spyro::cutscene_scene_recipe::read(mC);
    spyro::cutscene_scene_recipe::prepareFrame(mC, state);
  }
}

// STAGES 13 AND 14 compose the already-owned actor, RenderWorldChunks, and cyclorama producers in
// their authored order. Stage 13 adds its front-end sprites before those owners. Stage 14 copies
// its clear colour before nativeFrameBegin and adds its conditional screen fade afterward. Every
// producer refuses before partial submission when its semantic input cannot be represented.
void SpyroRenderer::renderScene(const Scene &sc) const {
  if (sc.stage != kStageFrontEnd && sc.stage != kStageCutscene) {
    abortUnimplemented(sc, "no producer is registered for this stage");
  }
  const int32_t ofsX = mC->mem_r16s(mEnv + 8u), ofsY = mC->mem_r16s(mEnv + 10u);
  const int32_t cx = mC->mem_r16s(mEnv + 0u), cy = mC->mem_r16s(mEnv + 2u);
  const int32_t cw = mC->mem_r16s(mEnv + 4u), ch = mC->mem_r16s(mEnv + 6u);
  if (sc.stage == kStageFrontEnd) {
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
  }
  if (!spyro_actor_submit(mC)) {
    abortUnimplemented(sc, "actor producer 0x8001F798 refused its atomic recipe");
  }
  int32_t worldSelection = -1;
  if (sc.stage == kStageFrontEnd) {
    const auto invocation = spyro::stage13_scene_recipe::sharedBackdropInvocation();
    spyro::stage13_scene_recipe::apply(mC, invocation);
    worldSelection = invocation.worldSelection;
  } else {
    const auto invocation = spyro::cutscene_scene_recipe::worldInvocation();
    spyro::cutscene_scene_recipe::applyWorldInvocation(mC, invocation);
    worldSelection = invocation.worldSelection;
  }
  if (!spyro_world_submit(mC, worldSelection)) {
    abortUnimplemented(sc, "world producer 0x800258F0 refused its atomic recipe");
  }
  if (!spyro_terrain_submit(mC, -1, kCamera + 0x14u, kCamera)) {
    abortUnimplemented(sc, "cyclorama producer 0x8004EBA8 refused its atomic recipe");
  }
  if (sc.stage == kStageCutscene) {
    const auto state = spyro::cutscene_scene_recipe::read(mC);
    const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
    const auto fade = spyro::screen_fade_recipe::cutscene(state.fade, ofsX, ofsY, renderWidth);
    if (!spyro_screen_fade_submit(mC, fade)) {
      abortUnimplemented(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
    }
  }
}

[[noreturn]] void SpyroRenderer::abortUnimplemented(const Scene &sc, const char *why) const {
  lucent::error(
      "render", "NATIVE RENDER NOT IMPLEMENTED — stage selector = {} ({})", sc.stage, why);
  lucent::error("render",
                "  fatal boundary: guest pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} "
                "stage={}/{}/{} load_stage={} state_switch={}",
                mC->pc,
                mC->r[31],
                mC->r[29],
                mC->mem_r32(kStageSelector),
                mC->mem_r32(kStageSubstate),
                mC->mem_r32(kStageSubSubstate),
                mC->mem_r32(kLoadStage),
                mC->mem_r32(kStateSwitch));
  for (const auto &slot : mC->cfg->overlaySlots) {
    if (slot.base == 0u) {
      continue;
    }
    const char *resident = overlay_router_resident_name(mC, slot.base);
    lucent::error("render",
                  "  resident overlay slot 0x{:08X}: {}",
                  slot.base,
                  resident != nullptr ? resident : "(outside configured slot)");
  }
  reportBacklog(sc);
  lucent::error("render",
                "  no fallback is installed on purpose: a native branch that drew "
                "something plausible would make this gap invisible. Port the scene above, "
                "or first split the retained diagnostic renderer's guest-VSync tail.");
  abort();
}

// ONE frame's picture.
void SpyroRenderer::drawFrame() {
  const Scene sc = classifyScene();
  auto &paired = spyro_paired_actor_state(mC);
  Fps60 &temporal = fps60(*mC->game);
  const bool pairedState = pairedActorScene(mC, sc);
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
    // Publish ownership before entering the retained body. Today the mandatory VSync trap stops
    // the diagnostic leg before it returns; this ordering is already correct for the future split
    // tail, where frame_commit below becomes its sole presenter.
    spyro_presentation_owner(*mC).beginGuestFrame();
    const bool pairedOracle =
        pairedState && (cfg_str("PSXPORT_PAIREDPOSE_ORACLE") != nullptr ||
                        cfg_str("PSXPORT_PAIRED_TRANSFORM_ORACLE") != nullptr ||
                        cfg_str("PSXPORT_PAIRED_FLOAT_XY_ORACLE") != nullptr);
    if (pairedOracle) {
      spyro_paired_actor_oracle_arm(mC);
    }
    referenceOtWalk();
    // Unreachable until the retained render-arm tails stop calling guest VSync. Once split, the
    // guest OT walk will have filled the capture and this fence will drain/present it exactly once.
    // The same two-field logic-frame quota as the native leg below — the reference leg reproduces
    // the guest's cadence, not just its pixels.
    temporal.frame_commit(mC, kFieldsPerLogicFrame);
    spyro1::acknowledgeTemporalCommit(*mC);
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
  prepareScene(sc);
  mEnv = nativeFrameBegin(mC);
  if (!mVideoModeAnnounced) {
    mVideoModeAnnounced = true;
    lucent::info("wide",
                 "native picture: aspect={} wide_engine={} native_width={} render_width={}",
                 mC->game->mods.aspect,
                 gpu_vk_wide_engine(mC),
                 mC->game->gpu.s_disp_w,
                 gpu_vk_wide_engine_w(mC));
  }
  renderScene(sc);
  if (!spyro_paired_actor_frame_finish(paired, false, pairedState)) {
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
  // THE NATIVE LEG PRESENTS ONLY THROUGH frame_commit — fps60CommitPending=true makes
  // FieldScheduler defer present, pace, and host-turn acknowledgement to that fence. This is
  // the Tomba2-aligned shape: "frame_commit OWNS presentation in both configs"
  // (game_tomba2.cpp:130). The OLD code gated frame_commit behind fps60.active() and let the field
  // service present when fps60 was off — which meant the framework's flush CAPTURE
  // (rq_capture, unconditional since the ONE PATH change) was never drained by presentRotate, so
  // fps60=0 accumulated every flush and overflow-aborted. That is fixed by making frame_commit the
  // single fence for both configs.
  nativeFrameEnd(mC, mEnv, true);
  // THE PER-LOGIC-FRAME FENCE. flush() CAPTURES into Fps60::mNCur in both configs and frame_commit
  // (present_vk -> presentRotate -> mNCur = 0) is the ONLY drain. guestFields=kFieldsPerLogicFrame
  // (2): the logic frame spends TWO display fields (30 Hz logic, the guest's own measured tail);
  // present_vk splits that quota across the extra lerped frame only when fps60 is active
  // (extraFrame = active() && mHavePrev). One field per logic frame ran the game at twice its
  // retail speed — boot fields paced per-field and were correct, which is exactly why the defect
  // only showed once gameplay handed pacing to this fence.
  temporal.frame_commit(mC, kFieldsPerLogicFrame);
  spyro1::acknowledgeTemporalCommit(*mC);
}
