// render_frame.cpp — ONE frame's picture: the reference OT walk, or the native producers.
//
// Spyro1FrameDriver calls this title seam directly. Scene producers feed one render queue, and
// frame_commit owns the presentation fence. GameHooks::drawOTag remains unset to avoid a second
// presentation route.
#include "actor_scene_oracle.h"
#include "core.h"
#include "cutscene_scene_recipe.h"
#include "field_moby_lists.h"
#include "fps60.h"     // checked access to Spyro 1's title-owned temporal presentation product
#include "frame_env.h" // nativeFrameBegin/End — the frame the native producers draw into
#include "fx_actor_draw.h"
#include "fx_field_actor_composition.h"
#include "fx_field_collectables.h"
#include "fx_field_cyclorama.h"
#include "fx_field_environment.h"
#include "fx_field_particles.h"
#include "fx_field_player_actor.h"
#include "fx_field_shadow.h"
#include "fx_field_tracers.h"
#include "fx_moby_shadow.h"
#include "fx_paired_actor.h"
#include "fx_screen_border.h"
#include "fx_screen_fade.h"
#include "fx_spyro_flame.h"
#include "fx_world_draw.h"
#include "game.h"       // Game::rq — the render queue the native producers emit into
#include "gpu_vk.h"     // measured native/wide engine extents for the product-path announcement
#include "guest_call.h" // Bounded runtime execution of the retained reference driver.
#include "presentation_owner.h"
#include "render.h"
#include "screen_fade_recipe.h"
#include "snapshot.h" // snapshot_now — a refusal fatal must leave the corpus its fix needs
#include "spyro1_field_scheduler.h"
#include "spyro_game.h"
#include "stage13_scene_recipe.h"
#include "temporal_scene.h"
#include <array>
#include <lucent/log.h>
#include <stdlib.h> // abort

namespace {
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kStageSelector = 0x800757D8u;
constexpr uint32_t kStageSubstate = 0x80078D78u;
constexpr uint32_t kStageSubSubstate = 0x80078D7Cu;
constexpr uint32_t kStateSwitch = 0x8007579Cu;
constexpr uint32_t kLoadStage = 0x80075864u;
constexpr uint32_t kGameplayDrawFrame = 0x8007593Cu;

bool isFieldStage(uint32_t stage) {
  return stage == kStageField || stage == kStageRespawn || stage == kStageGameOver;
}

bool pairedActorScene(Core *core, const Scene &scene) {
  const bool frontend = scene.stage == kStageFrontEnd && core->mem_r32(0x80078D78u) == 3u &&
                        core->mem_r32(0x80078D7Cu) == 2u;
  const bool respawnFading = (scene.stage == kStageRespawn || scene.stage == kStageGameOver) &&
                             core->mem_r32(kGameplayDrawFrame) != 0u;
  return frontend ||
         (isFieldStage(scene.stage) && !respawnFading && spyro_field_player_visible(core));
}
} // namespace

// The framework owns configuration; this entry announces the title's render policy.
void SpyroRenderer::installModeFromConfig(Core *c) {
  if (!c->rsub.mode.psxRender()) {
    lucent::info("render",
                 "native path: stage {} front-end and stage {} cutscene recipes. Aborts on a stage "
                 "with no producer or when a complete native recipe is refused. "
                 "PSXPORT_RENDER_PATH=gte for the reference picture (guest driver 0x8001ED5C).",
                 (int)kStageFrontEnd,
                 (int)kStageCutscene);
  }
}

// THE RETAINED REFERENCE BODY — a diagnostic entry for native-producer comparison. It is not
// currently a runnable player path: every reached retail render arm owns a VSync-based display
// tail, and the mandatory guest-VSync trap stops it until those diagnostic tails are split from
// scene production.
//
// The OT walk is inside it rather than beside it: the guest's driver ends in its own DrawOTag,
// which reaches the GPU through DMA2, and the framework walks the ordering table there
// (gpu_native.cpp `GpuState::gpu_dma2_linked_list`).
//
// The DMA2 ordering-table walk owns its queue flush. Flushing again here would re-emit the same
// consumed queue; see issue 0053 for the historical duplicate-submission finding.
void SpyroRenderer::referenceOtWalk() const {
  // This deliberately reaches the fatal VSync trap today. Do not add a success override here;
  // preserve the original guest body and split the measured display tail when the diagnostic leg is
  // made runnable again.
  psx::cpu::dispatchGuestToReturn0(
      *mC, kFrameRenderDrv, psx::cpu::ExecutionBudget::currentTurn(*mC), "frame-render-drv");
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
//
// STAGE 0 (GS_Playing) composes the field-snapshot owners issue 0089 captured, in the guest's
// authored draw order (external/spyro-1 src/gamestates/draw.c GamestateDraw, GS_Playing branch):
// cyclorama clear colour; collectables unless a flight level (0x80019300, g_IsFlightLevel
// 0x80075690); the moby chains (0x80019698); the environment and world chunks (0x8002B9CC ->
// 0x800258F0); the cyclorama wrapper (0x80050BD0); particles (0x800573C8); the screen fade; the
// screen border (0x80018F30); tracers (0x800189F0). Each layer now has an atomic native recipe; a
// refusal remains fail-fast at the first incomplete input rather than silently dropping it.
void SpyroRenderer::renderScene(const Scene &sc) const {
  const int32_t ofsX = mC->mem_r16s(mEnv + 8u), ofsY = mC->mem_r16s(mEnv + 10u);
  const int32_t cx = mC->mem_r16s(mEnv + 0u), cy = mC->mem_r16s(mEnv + 2u);
  const int32_t cw = mC->mem_r16s(mEnv + 4u), ch = mC->mem_r16s(mEnv + 6u);
  if (isFieldStage(sc.stage)) {
    if ((sc.stage == kStageRespawn || sc.stage == kStageGameOver) &&
        mC->mem_r32(kGameplayDrawFrame) != 0u) {
      const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
      const auto fade =
          spyro::screen_fade_recipe::field(mC->mem_r32(0x80075918u), ofsX, ofsY, renderWidth);
      if (!spyro_screen_fade_submit(mC, fade)) {
        abortUnimplemented(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
      }
      if (!spyro_screen_border_submit(mC, ofsX, ofsY, renderWidth)) {
        abortUnimplemented(sc, "screen border producer 0x80018F30 refused its atomic recipe");
      }
      return;
    }
    const auto background = spyro::cutscene_scene_recipe::read(mC);
    spyro::cutscene_scene_recipe::prepareFrame(mC, background);
    constexpr uint32_t kIsFlightLevel = 0x80075690u;
    // Retail clears g_SonyImage.m_ShadedMobys immediately before func_80019300. The native actor
    // producer does not consume that guest pointer list, so reproduce the lifecycle boundary here
    // instead of letting a prior screen's stale entries consume the field HUD capacity.
    mC->mem_w32(0x800720F4u, 0u);
    // Retail builds the three actor lists before the regular, secondary, and shaded passes. The
    // regular native owner reads the level array directly, but the secondary owner consumes the
    // negative-state list produced here; omitting this state-only arm leaves its source pointers
    // uninitialized at the first FIELD frame.
    spyro_field_build_moby_lists(mC);
    if (mC->mem_r32(kIsFlightLevel) == 0u) {
      if (!spyro_field_collectables_submit(mC)) {
        abortUnimplemented(sc, "collectables producer 0x80019300 refused its atomic recipe");
      }
    }
    if (!spyro_actor_submit(mC)) {
      abortUnimplemented(sc, "actor producer 0x80019698 refused its atomic recipe");
    }
    if (!spyro_field_actor_composition_submit(mC)) {
      abortUnimplemented(sc,
                         "secondary/shaded actor producers 0x80020F34/0x80022A2C refused their "
                         "combined atomic recipe");
    }
    // 0x80019698 draws the moby shadows between the shaded pass and Spyro's own model, so this
    // layer belongs here rather than beside the Spyro shadow it superficially resembles.
    if (!spyro_moby_shadow_submit(mC)) {
      abortUnimplemented(sc, "moby shadow producer 0x80059F8C refused its atomic recipe");
    }
    if (!spyro_field_player_submit(mC, spyro_paired_actor_state(mC))) {
      abortUnimplemented(sc, "Spyro actor producer 0x80023AC4 refused its atomic recipe");
    }
    if (!spyro_field_shadow_submit(mC)) {
      abortUnimplemented(sc, "Spyro shadow producer 0x80059A48 refused its atomic recipe");
    }
    // 0x80019698 calls the flame last of the model layers, only while the flame is active, and
    // after Spyro's own producer has published the orientation it reads.
    if (!spyro_flame_submit(mC)) {
      abortUnimplemented(sc, "Spyro flame producer 0x80058D64 refused its atomic recipe");
    }
    // Diagnostic only and a no-op unless PSXPORT_ACTOR_SCENE_ORACLE=1. It runs retail's moby-chain
    // walker over the state the native producers have just read, so it must sit after every
    // producer that walker covers, and must never be armed on a shipping frame. Retail draws the
    // player and its shadow as ordinary mobys, so those two producers are part of the comparison
    // even though the port owns them separately — which is why the call is here and not before
    // them. The printed painter histogram is what makes the five-way split readable.
    static constexpr std::array<uint32_t, 7> kActorPainters = {
        0x8001F798u, 0x80020F34u, 0x80022A2Cu, 0x80023AC4u, 0x80059A48u, 0x80059F8Cu, 0x80058D64u};
    spyro::actor_scene_oracle::compare(mC, 0x80019698u, kActorPainters, "actor-scene-oracle");
    if (!spyro_field_environment_submit(mC)) {
      abortUnimplemented(sc, "environment producer 0x8002B9CC refused its atomic recipe");
    }
    if (!spyro_field_cyclorama_submit(mC)) {
      abortUnimplemented(sc, "cyclorama producer 0x80050BD0 refused its atomic recipe");
    }
    if (!spyro_field_particles_submit(mC)) {
      abortUnimplemented(sc,
                         "particles producer 0x800573C8 refused its atomic type-0/type-2 recipe");
    }
    const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
    const auto fade =
        spyro::screen_fade_recipe::field(mC->mem_r32(0x80075918u), ofsX, ofsY, renderWidth);
    if (!spyro_screen_fade_submit(mC, fade)) {
      abortUnimplemented(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
    }
    if (!spyro_screen_border_submit(mC, ofsX, ofsY, renderWidth)) {
      abortUnimplemented(sc, "screen border producer 0x80018F30 refused its atomic recipe");
    }
    if (!spyro_field_tracers_submit(mC)) {
      abortUnimplemented(sc, "tracers producer 0x800189F0 refused its atomic recipe");
    }
    return;
  }
  if (sc.stage != kStageFrontEnd && sc.stage != kStageCutscene) {
    abortUnimplemented(sc, "no producer is registered for this stage");
  }
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
    const auto identity = mC->currentImageIdentity(slot.base);
    lucent::error("render",
                  "  resident overlay slot 0x{:08X}: id={}",
                  slot.base,
                  identity ? identity->id : 0);
  }
  reportBacklog(sc);
  // The frame-MISS path already dumps 2 MB of guest RAM because that image is what every
  // follow-up question gets answered from. A refusal fatal is the same kind of event and had no
  // dump, so each unowned stage-0 layer had to be re-driven live to be looked at once. Write it
  // here unconditionally: this path ends the run anyway, so there is no cost to weigh.
  snapshot_now(mC, "native-render-refusal");
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
  const uint64_t temporalScene = (uint64_t{sc.stage} << 1u) | uint64_t{pairedState};
  spyro_temporal_scene_begin(
      *mC, temporalScene, pairedState, mC->rsub.mode.psxRender(), temporal.active());
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
    referenceOtWalk();
    // Unreachable until the retained render-arm tails stop calling guest VSync. Once split, the
    // guest OT walk will have filled the capture and this fence will drain/present it exactly once.
    // The same two-field logic-frame quota as the native leg below — the reference leg reproduces
    // the guest's cadence, not just its pixels.
    temporal.frame_commit(mC, kFieldsPerLogicFrame);
    spyro1::acknowledgeTemporalCommit(*mC);
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
  spyro_temporal_scene_prepare(*mC);
  // Submit the complete native scene once, after every producer has accepted its input.
  mC->game->rq.flush(mC);
  // …and show the buffer this env names. The guest's own tail is PutDispEnv(activeEnv + 0x5C); see
  // frame_env.cpp for why that displays the PREVIOUS iteration's buffer and why that is correct.
  //
  // Defer presentation, pacing, and host-turn acknowledgement to frame_commit in both temporal
  // modes. It drains the capture accumulated by the queue flush.
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
