#include "fx_dragon_scene.h"

#include "core.h"
#include "dragon_scene_recipe.h"
#include "field_moby_lists.h"
#include "field_model_chain.h"
#include "fx_actor_draw.h"
#include "fx_dragon_burst.h"
#include "fx_field_actor_composition.h"
#include "fx_field_cyclorama.h"
#include "fx_field_environment.h"
#include "fx_field_particles.h"
#include "fx_field_player_actor.h"
#include "fx_field_shadow.h"
#include "fx_moby_shadow.h"
#include "fx_paired_actor.h"
#include "fx_screen_border.h"
#include "fx_screen_fade.h"
#include "guest_call.h"
#include "screen_fade_recipe.h"

#include <lucent/log.h>

namespace {

using spyro::dragon_scene::Producer;

constexpr unsigned kRecipeRefusal = 1u;
constexpr uint32_t kRescuedText = 0x80018728u;
constexpr uint32_t kCopyHudMobys = 0x80018880u;
constexpr uint32_t kQueueMobys = 0x800521C0u;

// The draw-state shake vectors state 5 scales, and the ramp it scales them by.
constexpr uint32_t kShakeEnable = 0x8006FCF4u + 0x1600u;
constexpr uint32_t kShakeOffset = 0x8006FCF4u + 0x161Cu;
constexpr uint32_t kShakeAngle = 0x8006FCF4u + 0x1622u;
constexpr uint32_t kShakeRamp = 0x8006F3C0u;
constexpr uint32_t kShakeRampEntries = 32u;
constexpr uint32_t kSineTable = 0x8006CBF8u;
constexpr int32_t kShakeAngleTicks = 8;
constexpr int32_t kShakeAngleStep = 42;

int16_t fixedMul(int16_t value, int32_t scale) {
  return (int16_t)(((int32_t)value * scale) >> 12);
}

void scaleVector(Core *core, uint32_t address, int32_t scale) {
  for (uint32_t lane = 0; lane < 3u; ++lane) {
    const uint32_t at = address + lane * 2u;
    core->mem_w16(at, (uint16_t)fixedMul((int16_t)core->mem_r16(at), scale));
  }
}

// State 5 decays the cutscene camera shake in place before drawing: the offset by a 32-entry ramp
// indexed by the cutscene tick, and after eight ticks the angle by a cosine that turns the decay
// into a wobble. It is guest state rather than geometry, which is why it is a step of the
// composition and not part of any producer's recipe.
void cameraShake(Core *core, int32_t ticks) {
  if (core->mem_r32(kShakeEnable) == 0u) {
    return;
  }
  if (ticks < 0 || (uint32_t)ticks >= kShakeRampEntries) {
    // Retail indexes the ramp with no bound of its own. Refusing to read past it keeps the decay
    // frozen rather than scaling by whatever follows the table.
    return;
  }
  scaleVector(core, kShakeOffset, (int16_t)core->mem_r16(kShakeRamp + (uint32_t)ticks * 2u));
  if (ticks > kShakeAngleTicks) {
    const uint32_t entry = (uint32_t)((ticks - kShakeAngleTicks) * kShakeAngleStep) & 0xffu;
    // Cos(n) reads the shared table one quarter turn past the sine entry.
    const int32_t cosine = (int16_t)core->mem_r16(kSineTable + ((entry * 2u + 0x80u) & 0x1ffu));
    scaleVector(core, kShakeAngle, cosine);
  }
}

} // namespace

unsigned dragon_scene_submit(Core *core, int drawOffsetX, int drawOffsetY, int renderWidth) {
  if (core == nullptr) {
    return kRecipeRefusal;
  }
  const auto state = spyro::dragon_scene::read(core);
  // 0x8001CFDC runs the burst 0x80058864 before every branch, gated on D_80076248's enable word.
  // It links into the HUD ordering table rather than the world one, so it is a 2D overlay drawn
  // over whichever state composes below.
  if (!dragon_burst_submit(core)) {
    return 0x80058864u;
  }
  const auto plan = spyro::dragon_scene::plan(core, state);
  if (plan.status != spyro::dragon_scene::Status::Ready) {
    lucent::debug("dragon",
                  "REFUSED plan={} state={} ticks={}",
                  spyro::dragon_scene::statusName(plan.status),
                  state.state,
                  state.ticks);
    return kRecipeRefusal;
  }
  spyro::dragon_scene::commit(core, plan);
  lucent::Line census;
  census.add("state={} ticks={} fade={} draw={} shaded={} steps={}",
             state.state,
             state.ticks,
             state.fade,
             plan.lists.draw.size(),
             plan.lists.shaded.size(),
             plan.producers.size());

  const auto budget = [core]() {
    return psx::cpu::ExecutionBudget::currentTurn(*core);
  };
  spyro::actor_scene::Source source{};
  if (plan.explicitDrawSource) {
    source = {spyro::actor_scene::Source::Kind::ExplicitList, spyro::dragon_scene::kDrawList};
  }
  for (const auto producer : plan.producers) {
    bool ok = true;
    switch (producer) {
    case Producer::QueueMobys:
      spyro_field_build_moby_lists(core);
      break;
    case Producer::RescuedText:
      psx::cpu::dispatchGuestToReturn0(*core, kRescuedText, budget(), "dragon-rescued-text");
      break;
    case Producer::CopyHudMobys:
      psx::cpu::dispatchGuestToReturn0(*core, kCopyHudMobys, budget(), "dragon-copy-hud-mobys");
      break;
    case Producer::CameraShake:
      cameraShake(core, state.ticks);
      break;
    case Producer::ClearDrawList:
      spyro::dragon_scene::clearDrawList(core);
      break;
    case Producer::Regular:
      ok = spyro_actor_submit(core, source);
      break;
    case Producer::Secondary:
      ok = spyro_field_actor_composition_submit(core, {.secondary = true, .shaded = false});
      break;
    case Producer::Shaded:
      ok = spyro_field_actor_composition_submit(core, {.secondary = false, .shaded = true});
      break;
    case Producer::MobyShadows:
      ok = spyro_moby_shadow_submit(core);
      break;
    case Producer::SpyroModel:
      // Retail calls 0x80023AC4 straight, with no g_IsSpyroHidden test: only state 0 reaches the
      // player through 0x80019698, which owns that gate itself. Routing these branches through the
      // field player owner would add a hide check the cutscene does not have.
      ok = spyro_paired_actor_submit_field(core, spyro_paired_actor_state(core));
      break;
    case Producer::SpyroShadow:
      ok = spyro_field_shadow_submit(core);
      break;
    case Producer::Environment:
      ok = spyro_field_environment_submit(core);
      break;
    case Producer::Cyclorama:
      ok = spyro_field_cyclorama_submit(core);
      break;
    case Producer::Particles:
      ok = spyro_field_particles_submit(core);
      break;
    case Producer::ScreenFade:
      ok = spyro_screen_fade_submit(
          core,
          spyro::screen_fade_recipe::dragon(
              (uint32_t)state.fade, drawOffsetX, drawOffsetY, renderWidth));
      break;
    case Producer::ScreenBorder:
      ok = spyro_screen_border_submit(core, drawOffsetX, drawOffsetY, renderWidth);
      break;
    case Producer::FieldChain:
      if (const unsigned refused = spyro_field_model_chain_submit(core); refused != 0u) {
        census.add(" REFUSED at 0x{:08X}", refused);
        census.flush_debug("dragon");
        return refused;
      }
      break;
    }
    if (!ok) {
      census.add(" REFUSED at {}", spyro::dragon_scene::producerName(producer));
      census.flush_debug("dragon");
      return kRecipeRefusal;
    }
  }
  census.flush_debug("dragon");
  return 0u;
}

bool spyro_dragon_scene_draws_player(Core *core) {
  if (core == nullptr) {
    return false;
  }
  const spyro::dragon_scene::State state = spyro::dragon_scene::read(core);
  const spyro::dragon_scene::Plan plan = spyro::dragon_scene::plan(core, state);
  if (plan.status != spyro::dragon_scene::Status::Ready) {
    return false;
  }
  for (const auto producer : plan.producers) {
    if (producer == spyro::dragon_scene::Producer::SpyroModel) {
      return true;
    }
    // State 0 reaches the player through the shared field chain, which applies 0x80019698's own
    // hide gate, so the answer there is the field answer.
    if (producer == spyro::dragon_scene::Producer::FieldChain) {
      return spyro_field_player_visible(core);
    }
  }
  return false;
}
