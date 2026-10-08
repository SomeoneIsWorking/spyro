// frame_renderer.cpp — one frame's picture: the reference OT walk, or the native producers.
#include "frame_renderer.h"
#include "actor_producer.h"
#include "core.h"
#include "cutscene_scene_recipe.h"
#include "demo_text_scene.h"
#include "dragon_scene_producer.h"
#include "fairy_menu_scene.h"
#include "field_2d_overlay.h"
#include "field_collectables.h"
#include "field_cyclorama.h"
#include "field_environment.h"
#include "field_moby_lists.h"
#include "field_model_chain.h"
#include "field_particles.h"
#include "field_player_actor.h"
#include "field_shadow.h"
#include "field_tracers.h"
#include "fps60.h"      // checked access to Spyro 1's title-owned temporal presentation product
#include "frame_env.h"  // spyro::render::frameBegin/End — the frame the native producers draw into
#include "game.h"       // Game::rq — the render queue the native producers emit into
#include "gpu_vk.h"     // measured native/wide engine extents for the product-path announcement
#include "guest_call.h" // Bounded runtime execution of the retained reference driver.
#include "guest_globals.h"
#include "guest_scene.h"
#include "level_transition_scene.h"
#include "native_terrain.h"
#include "paired_actor.h"
#include "paired_actor_temporal.h"
#include "pause_menu_scene.h"
#include "presentation_owner.h"
#include "screen_border.h"
#include "screen_fade.h"
#include "screen_fade_recipe.h"
#include "snapshot.h" // snapshot_now — a refusal fatal must leave the corpus its fix needs
#include "spyro1_field_scheduler.h"
#include "spyro_context.h"
#include "stage13_scene_recipe.h"
#include "temporal_scene.h"
#include "world_producer.h"
#include <array>
#include <lucent/log.h>
#include <string>

namespace {
using spyro::guest::kCamera;
// The scene vocabulary frame_renderer.h owns.
using spyro::guest::kGamestate;
using spyro::guest::kTitlescreenState;
using spyro::render::kFrameRenderDrv;
using spyro::render::kStageCutscene;
using spyro::render::kStageDragon;
using spyro::render::kStageEntranceAnimation;
using spyro::render::kStageFairy;
using spyro::render::kStageField;
using spyro::render::kStageFrontEnd;
using spyro::render::kStageGameOver;
using spyro::render::kStageInventoryMenu;
using spyro::render::kStageLevelTransition;
using spyro::render::kStageOldDragon;
using spyro::render::kStagePauseMenu;
using spyro::render::kStageRespawn;
using spyro::render::Scene;
constexpr uint32_t kStageSubSubstate = 0x80078D7Cu;
using spyro::guest::kLoadStage;
using spyro::guest::kStateSwitch;
constexpr uint32_t kGameplayDrawFrame = 0x8007593Cu;

// One message per layer of 0x80019698, naming the producer that refused.
const char *modelChainRefusal(unsigned producer) {
  switch (producer) {
  case 0x8001F798u:
    return "actor producer 0x8001F798 refused its atomic recipe";
  case 0x80020F34u:
    return "secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic "
           "recipe";
  case 0x80059F8Cu:
    return "moby shadow producer 0x80059F8C refused its atomic recipe";
  case 0x80023AC4u:
    return "Spyro actor producer 0x80023AC4 refused its atomic recipe";
  case 0x80059A48u:
    return "Spyro shadow producer 0x80059A48 refused its atomic recipe";
  case 0x80058D64u:
    return "Spyro flame producer 0x80058D64 refused its atomic recipe";
  case 0x80058BA8u:
    return "glow/sparkle producer 0x80058BA8 refused its atomic recipe";
  default:
    return "a producer of 0x80019698 refused its atomic recipe";
  }
}

std::string refusalMessage(const char *layer, const spyro::ProducerRefusal &refusal) {
  return refusal.detail.empty() ? std::string(layer)
                                : lucent::format("{} — {}", layer, refusal.detail);
}

// The dragon owner reports a guest address, or 1 when the composition itself could not be derived.
const char *dragonRefusal(unsigned producer) {
  if (producer == 0x80058864u) {
    return "dragon burst producer 0x80058864 is armed and has no native owner";
  }
  if (producer == 1u) {
    return "dragon cutscene producer 0x8001CFDC refused its atomic composition";
  }
  return modelChainRefusal(producer);
}

bool isFieldStage(uint32_t stage) {
  return stage == kStageField || stage == kStageRespawn || stage == kStageGameOver;
}

bool pairedActorScene(Core *core, const Scene &scene) {
  const bool frontend = scene.stage == kStageFrontEnd &&
                        core->mem_r32(spyro::guest::kTitlescreenState) == 3u &&
                        core->mem_r32(0x80078D7Cu) == 2u;
  const bool respawnFading = (scene.stage == kStageRespawn || scene.stage == kStageGameOver) &&
                             core->mem_r32(kGameplayDrawFrame) != 0u;
  // The dragon cutscene draws Spyro too — through the shared field chain in its state 0 and
  // through 0x80023AC4 directly in most of the rest — so it arms the same ownership gate. Its own
  // state table answers, rather than a second copy of it here.
  const bool dragon = scene.stage == kStageDragon && spyro::dragon_scene::drawsPlayer(core);
  // src/gamestates/draw.c:816 — func_8001A050 calls 0x80023AC4 unconditionally, so both stages it
  // serves always arm the same ownership gate as a field stage.
  const bool levelTransition =
      scene.stage == kStageLevelTransition || scene.stage == kStageEntranceAnimation;
  // 0x8001A40C — the pause / inventory / old-dragon handler — reaches the same whole 0x80019698
  // chain on its world path, so it arms the same gate through the same field-player question. The
  // menu's own frames carry no separate ownership: the world behind the menu is the field's world.
  // 0x8001D718 (GS_Fairy) runs the same world calls under its dialogue.
  const bool menuArm = scene.stage == kStagePauseMenu || scene.stage == kStageInventoryMenu ||
                       scene.stage == kStageOldDragon || scene.stage == kStageFairy;
  return frontend || dragon || levelTransition ||
         ((isFieldStage(scene.stage) || menuArm) && !respawnFading &&
          spyro::field_player_actor::visible(core));
}
} // namespace

spyro::render::FrameRenderer::FrameRenderer(Core *c,
                                            spyro::render::SpriteQueueOffsetObserver *queueObserver)
    : mC(c), mQueueObserver(queueObserver) {}

// The framework owns configuration; this entry announces the title's render policy.
void spyro::render::FrameRenderer::installModeFromConfig(Core *c) {
  if (!c->rsub.mode.psxRender()) {
    lucent::info("render",
                 "native path: stage {} front-end and stage {} cutscene recipes. Aborts on a stage "
                 "with no producer or when a complete native recipe is refused. "
                 "PSXPORT_RENDER_PATH=gte for the reference picture (guest driver 0x8001ED5C).",
                 (int)kStageFrontEnd,
                 (int)kStageCutscene);
  }
}

// The retained reference body, a diagnostic entry for native-producer comparison. Every reached
// retail render arm owns a VSync display tail, and the mandatory guest-VSync trap stops the leg;
// the OT walk lives inside it because the guest's driver ends in its own DrawOTag over DMA2, and
// that walk owns its queue flush.
void spyro::render::FrameRenderer::referenceOtWalk() const {
  // Reaches the fatal VSync trap by design: preserve the original guest body and split its
  // display tail when the diagnostic leg is made runnable again.
  psx::cpu::dispatchGuestToReturn0(
      *mC, kFrameRenderDrv, psx::cpu::ExecutionBudget::currentTurn(*mC), "frame-render-drv");
}

// Render-only state a stage needs before the frame's draw env is programmed.
void spyro::render::FrameRenderer::prepareScene(const Scene &sc) const {
  if (sc.stage == kStageCutscene || sc.stage == kStageLevelTransition ||
      sc.stage == kStageEntranceAnimation) {
    const auto state = spyro::cutscene_scene_recipe::read(mC);
    spyro::cutscene_scene_recipe::prepareFrame(mC, state);
  }
}

// One branch per stage that has a producer, composing its layers in the guest's authored draw
// order. Any other stage — and any stage whose producer refuses its current input — is handed to
// the guest renderer: unported guest behaviour through Lightrec is this product's architecture.
spyro::render::SceneOwner spyro::render::FrameRenderer::renderScene(const Scene &sc) {
  const int32_t ofsX = mC->mem_r16s(mEnv + 8u), ofsY = mC->mem_r16s(mEnv + 10u);
  const int32_t cx = mC->mem_r16s(mEnv + 0u), cy = mC->mem_r16s(mEnv + 2u);
  const int32_t cw = mC->mem_r16s(mEnv + 4u), ch = mC->mem_r16s(mEnv + 6u);
  if (isFieldStage(sc.stage)) {
    if ((sc.stage == kStageRespawn || sc.stage == kStageGameOver) &&
        mC->mem_r32(kGameplayDrawFrame) != 0u) {
      // The respawn/game-over arms are not a field arm: they draw a fade and a border over a
      // frozen gameplay frame.
      const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
      const auto fade =
          spyro::screen_fade_recipe::field(mC->mem_r32(0x80075918u), ofsX, ofsY, renderWidth);
      if (!spyro::screen_fade::submit(mC, mC->game->rq, fade)) {
        return guestFallback(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
      }
      if (spyro::screen_border::armed(mC)) {
        const auto border = spyro::screen_border::stage(mC);
        if (!spyro::screen_border::submit(mC, mC->game->rq, border)) {
          return guestFallback(sc, "screen border producer 0x80018F30 refused its atomic recipe");
        }
      }
      return SceneOwner::NativeProducers;
    }
    const auto background = spyro::cutscene_scene_recipe::read(mC);
    spyro::cutscene_scene_recipe::prepareFrame(mC, background);
    // Retail clears g_SonyImage.m_ShadedMobys immediately before func_80019300, and the native
    // actor producer does not consume that list; reproduce the lifecycle boundary here.
    mC->mem_w32(0x800720F4u, 0u);
    // Retail builds the three actor lists before the regular, secondary and shaded passes. The
    // secondary owner consumes the negative-state list produced here.
    spyro::field_moby_lists::build(mC);
    // The 2D overlay's endpoint, captured once for the whole frame: the three producers below must
    // read one consistent pair of endpoints, not three reads that could straddle a guest write.
    const int32_t fieldRenderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
    auto &overlay = spyro::context(*mC).overlayFrame;
    const auto overlayStatus = overlay.capture(*mC, ofsX, ofsY, fieldRenderWidth);
    if (overlayStatus == spyro::field_2d_overlay::Status::InvalidCount) {
      return guestFallback(sc, "collectables producer 0x80019300 refused its atomic recipe");
    }
    if (overlay.armed(spyro::field_2d_overlay::Part::Sprite)) {
      if (!overlay.commit(*mC, spyro::field_2d_overlay::Part::Sprite)) {
        return guestFallback(sc, "collectables producer 0x80019300 refused its atomic recipe");
      }
      if (!spyro::field_2d_overlay::publish(
              *mC, mC->game->rq, spyro::field_2d_overlay::Part::Sprite, overlay.overlay())) {
        return guestFallback(sc, "collectables producer 0x80019300 refused its atomic recipe");
      }
    }
    // 0x8001F000: the attract demo's "DEMO MODE" caption, between the collectables and the actor
    // pass, so the shaded pass the model chain runs finds it in the queue.
    if (!spyro::demo_text_scene::submit(mC)) {
      return guestFallback(sc, "demo-mode text producer 0x80018908 refused its atomic recipe");
    }
    if (const auto refusal = spyro::field_model_chain::submit(mC)) {
      return guestFallback(sc,
                           refusalMessage(modelChainRefusal(refusal.producer), refusal).c_str());
    }
    if (!spyro::field_environment::submit(mC)) {
      return guestFallback(sc, "environment producer 0x8002B9CC refused its atomic recipe");
    }
    if (!spyro::field_cyclorama::submit(mC)) {
      return guestFallback(sc, "cyclorama producer 0x80050BD0 refused its atomic recipe");
    }
    if (const auto refusal = spyro::field_particles::submit(mC)) {
      return guestFallback(sc, refusalMessage("particles", refusal).c_str());
    }
    // The fade and the border at the guest's own later positions, from the same endpoint the
    // sprites were published from. The border's stepped height is committed here, not at the
    // capture.
    if (overlay.armed(spyro::field_2d_overlay::Part::Fade)) {
      if (!overlay.commit(*mC, spyro::field_2d_overlay::Part::Fade) ||
          !spyro::field_2d_overlay::publish(
              *mC, mC->game->rq, spyro::field_2d_overlay::Part::Fade, overlay.overlay())) {
        return guestFallback(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
      }
    }
    if (overlay.armed(spyro::field_2d_overlay::Part::Border)) {
      if (!overlay.commit(*mC, spyro::field_2d_overlay::Part::Border) ||
          !spyro::field_2d_overlay::publish(
              *mC, mC->game->rq, spyro::field_2d_overlay::Part::Border, overlay.overlay())) {
        return guestFallback(sc, "screen border producer 0x80018F30 refused its atomic recipe");
      }
    }
    // Retained only after the whole layer is committed, so a frame whose guest state has not
    // finished being produced is never retained.
    spyro::context(*mC).overlayTemporal.retain(overlay.endpoint());
    if (!spyro::field_tracers::submit(mC)) {
      return guestFallback(sc, "tracers producer 0x800189F0 refused its atomic recipe");
    }
    return SceneOwner::NativeProducers;
  }
  // Stages 1 (GS_LevelTransition) and 9 (GS_EntranceAnimation), func_8001A050: the cleared Sony
  // image, the tally, the shaded HUD mobys, the Spyro actor and the cyclorama all belong to its
  // own owner.
  if (sc.stage == kStageLevelTransition || sc.stage == kStageEntranceAnimation) {
    const auto refusal = spyro::level_transition_scene::submit(mC);
    if (refusal != spyro::level_transition_scene::Refusal::None) {
      return guestFallback(sc, spyro::level_transition_scene::refusalName(refusal));
    }
    return SceneOwner::NativeProducers;
  }
  // Stages 2, 3 and 6 share one handler, 0x8001A40C: the field arm's five world calls, plus a
  // translucent panel, a lit border and the page's captions. None of its 2D layers is a HUD layer.
  if (sc.stage == kStagePauseMenu || sc.stage == kStageInventoryMenu ||
      sc.stage == kStageOldDragon) {
    const auto refusal = spyro::pause_menu_scene::submit(mC, cx + cw - 1);
    if (refusal != spyro::pause_menu_scene::Refusal::None) {
      return guestFallback(sc, spyro::pause_menu_scene::refusalName(refusal));
    }
    return SceneOwner::NativeProducers;
  }
  if (sc.stage == kStageFairy) {
    const auto refusal = spyro::fairy_menu_scene::submit(mC, cx + cw - 1);
    if (refusal != spyro::fairy_menu_scene::Refusal::None) {
      return guestFallback(sc, spyro::fairy_menu_scene::refusalName(refusal));
    }
    return SceneOwner::NativeProducers;
  }
  if (sc.stage == kStageDragon) {
    // 0x8001CFDC. The composition is one of eight authored branches selected by the cutscene's own
    // state, so the owner reports which layer refused rather than returning a bare false.
    const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
    if (const auto refusal = spyro::dragon_scene::submit(mC, ofsX, ofsY, renderWidth)) {
      return guestFallback(sc, refusalMessage(dragonRefusal(refusal.producer), refusal).c_str());
    }
    return SceneOwner::NativeProducers;
  }
  if (sc.stage != kStageFrontEnd && sc.stage != kStageCutscene) {
    return guestFallback(sc, "no producer is registered for this stage");
  }
  if (sc.stage == kStageFrontEnd) {
    const uint32_t titleMode = mC->mem_r32(spyro::guest::kTitlescreenState);
    if (!spyro::stage13_scene_recipe::hasSharedBackdrop(titleMode)) {
      if (!stage13Mode3Render()) {
        return guestFallback(sc, "mode 3 also armed paired-actor renderer 0x80023AC4");
      }
      return SceneOwner::NativeProducers;
    }
    if (!titleMenuRender(ofsX, ofsY, cx, cy, cx + cw - 1, cy + ch - 1)) {
      return guestFallback(sc, "the stage-13 producer declined this frame's menu mode");
    }
  }
  if (const auto refusal = spyro::actor_draw::submit(mC)) {
    return guestFallback(sc, refusalMessage("actor 0x8001F798", refusal).c_str());
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
  if (!spyro::world_draw::submit(mC, worldSelection)) {
    return guestFallback(sc, "world producer 0x800258F0 refused its atomic recipe");
  }
  if (!spyro::render::submitTerrainGuest(mC, -1, kCamera + 0x14u, kCamera)) {
    return guestFallback(sc, "cyclorama producer 0x8004EBA8 refused its atomic recipe");
  }
  if (sc.stage == kStageCutscene) {
    const auto state = spyro::cutscene_scene_recipe::read(mC);
    const int32_t renderWidth = gpu_vk_wide_engine(mC) ? gpu_vk_wide_engine_w(mC) : cw;
    const auto fade = spyro::screen_fade_recipe::cutscene(state.fade, ofsX, ofsY, renderWidth);
    if (!spyro::screen_fade::submit(mC, mC->game->rq, fade)) {
      return guestFallback(sc, "screen fade producer 0x800190D4 refused its atomic recipe");
    }
  }
  return SceneOwner::NativeProducers;
}

// The scene's identity, not producer input: the point is to name which scene the port has not
// composed and which guest function owns it.
void spyro::render::FrameRenderer::reportRefused(const Scene &sc, const char *why) const {
  lucent::error("render",
                "NATIVE RENDER NOT IMPLEMENTED — stage selector = {} ({}); drawing this scene "
                "with the GUEST renderer instead",
                sc.stage,
                why);
  lucent::error("render",
                "  guest state: pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} "
                "stage={}/{}/{} load_stage={} state_switch={}",
                mC->pc,
                mC->r[31],
                mC->r[29],
                mC->mem_r32(kGamestate),
                mC->mem_r32(kTitlescreenState),
                mC->mem_r32(kStageSubSubstate),
                mC->mem_r32(kLoadStage),
                mC->mem_r32(kStateSwitch));
  // A title need not declare a legacy GameConfig, and Spyro does not: `Core::cfg` is null for the
  // whole run. Say which case this is rather than skipping the overlay inventory quietly.
  if (mC->cfg == nullptr) {
    lucent::error("render",
                  "  resident overlay slots: none to report — this title declares no GameConfig");
  } else {
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
  }
  reportBacklog(sc);
}

// A refused scene goes to the guest renderer instead of abort(): the port's backlog and the
// product's playability are two questions, and a log line answers only the first. Reported once per
// run of the scene, because the guest fallback answers on every field it is on screen.
void spyro::render::FrameRenderer::drawSceneWithGuestFallback(const Scene &sc, const char *why) {
  const bool entering =
      !(mRefusedScene.reported && mRefusedScene.stage == sc.stage && mRefusedScene.why == why);
  if (entering) {
    if (mRefusedScene.reported) {
      lucent::error(
          "render",
          "left the unported scene: stage {} ({}) drew {} field(s) with the guest renderer",
          mRefusedScene.stage,
          mRefusedScene.why,
          mRefusedScene.frames);
    }
    mRefusedScene = {.stage = sc.stage, .why = why, .frames = 0, .reported = true};
    reportRefused(sc, why);
  }
  // The guest's own render arm, from the address the scene classifier recovered. A stage outside
  // 0..15, or the indirect arm 7 whose handler is data, has no guest body: the frame is presented
  // without this scene's prims, which is a visible gap rather than a dead product.
  const auto step = spyro::render::drawSceneWithGuestArm(*mC, sc.arm ? sc.arm->handler : 0u);
  ++mRefusedScene.frames;
  if (entering) {
    lucent::error("render",
                  "  guest renderer 0x{:08X}: {} after {} cycle(s), stopped at 0x{:08X}{}",
                  step.arm,
                  step.stop == spyro::render::GuestSceneStop::Drawn
                      ? "returned"
                      : (step.stop == spyro::render::GuestSceneStop::AskedForField
                             ? "asked for a display field at its display tail"
                             : "REFUSED"),
                  step.cycles,
                  step.guestPc,
                  step.reason.empty() ? std::string{} : std::string{" — "} + step.reason);
  }
}

void spyro::render::FrameRenderer::drawFrame() {
  const Scene sc = classifyScene();
  auto &paired = spyro::paired_actor::state(mC);
  Fps60 &temporal = fps60(*mC->game);
  const bool pairedState = pairedActorScene(mC, sc);
  const uint64_t temporalScene = (uint64_t{sc.stage} << 1u) | uint64_t{pairedState};
  spyro::temporal_scene::begin(
      *mC, temporalScene, pairedState, mC->rsub.mode.psxRender(), temporal.active());
  // Every drawn frame on both legs, so the scenes a real run actually reaches can be counted from
  // a log rather than guessed from the stage table.
  lucent::debug("scene",
                "stage={} leg={} arm={}",
                sc.stage,
                mC->rsub.mode.psxRender() ? "psx_render" : "native",
                sc.arm ? sc.arm->what : "(outside 0..15 — the guest draws nothing)");
  if (mC->rsub.mode.psxRender()) {
    // Publish ownership before entering the retained body: once the display tail is split out,
    // frame_commit below becomes the reference leg's sole presenter.
    spyro::presentationOwner(*mC).beginGuestFrame();
    referenceOtWalk();
    // The same two-field logic-frame quota as the native leg below: the reference leg reproduces
    // the guest's cadence, not just its pixels.
    temporal.frame_commit(mC, kFieldsPerLogicFrame);
    if (!spyro::paired_actor::frameFinish(paired, true, false)) {
      abort();
    }
    return;
  }
  // Boot upload-only screens intentionally leave the default at guest VRAM; reaching this frame
  // seam is the first point where the whole picture is known to come from producers.
  spyro::presentationOwner(*mC).beginNativeFrame();
  // Nothing flips the draw env on this leg, so the producers would emit into the buffer that is not
  // on screen; frameBegin owns that from the game's own DRAWENV.
  prepareScene(sc);
  mEnv = spyro::render::frameBegin(mC);
  const SceneOwner owner = renderScene(sc);
  if (owner == SceneOwner::GuestArm) {
    // The guest's arm composed the frame, so its prims are in guest VRAM and the present source
    // returns to guest VRAM. The render queue is not empty: the arm calls producers this port has
    // replaced, so the flush below is what puts them on screen.
    spyro::presentationOwner(*mC).beginGuestFrame();
  } else if (!spyro::paired_actor::frameFinish(paired, false, pairedState)) {
    abort();
  }
  spyro::temporal_scene::prepare(*mC);
  // Submit the complete scene once, after every producer has accepted its input. The guest arm's
  // native-override calls land in the same queue, so this is unconditional; an empty queue on a
  // guest-arm frame is the arm's own answer and captures nothing.
  mC->game->rq.flush(mC);
  // …and show the buffer this env names: the guest's own tail is PutDispEnv(activeEnv + 0x5C),
  // which displays the previous iteration's buffer. Presentation and pacing belong to frame_commit
  // in both temporal modes; it drains the capture accumulated by the queue flush.
  spyro::render::frameEnd(mC, mEnv, true);
  // The logic-frame fence. flush() captures into Fps60::mNCur and frame_commit is the only drain;
  // the two-field quota is the guest's cadence, split across the extra lerped frame only by
  // present_vk when fps60 is active.
  temporal.frame_commit(mC, kFieldsPerLogicFrame);
}
