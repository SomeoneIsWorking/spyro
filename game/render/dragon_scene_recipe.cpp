#include "dragon_scene_recipe.h"

#include "core.h"

namespace spyro::dragon_scene {
namespace {

// g_DragonCutscene = 0x80077030 (asm/data/game.bss.s); the field offsets follow include/dragon.h,
// whose 0x24-byte WAD header is confirmed by its own unk_0x40 comment naming 0x80077070.
constexpr uint32_t kCutscene = 0x80077030u;
constexpr uint32_t kState = 0x28u;
constexpr uint32_t kTicks = 0x3Cu;
constexpr uint32_t kFade = 0x44u;
constexpr uint32_t kCutsceneSpyro = 0x88u;
constexpr uint32_t kCutsceneDragon = 0x8Cu;
constexpr uint32_t kRescuedDragon = 0x90u;

constexpr uint32_t kScreenBorderEnabled = 0x8007570Cu;
constexpr uint32_t kBorderOverride = 0x800756C0u; // D_800756C0, the second border enable
constexpr uint32_t kBurstGate = 0x80076248u;      // D_80076248.unk_0x0
constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kHud = 0x80077FA8u;
constexpr uint32_t kHudMobys = 0x44u;
constexpr uint32_t kMobyBytes = 0x58u;
constexpr uint32_t kMobyClass = 54u;
constexpr uint32_t kMobyState = 72u;
constexpr uint32_t kMaxLevelMobys = 4096u;
// Retail's own "still visible" test: a state byte at or above 0x80 means the Moby is not drawn.
constexpr uint32_t kVisibleState = 0x80u;
constexpr int32_t kRescuedTextTicks = 60;
constexpr int32_t kFinalShadedOnlyTicks = 16;

void hudCounters(std::vector<uint32_t> &shaded) {
  for (uint32_t i = 0; i < kHudCounters; ++i) {
    shaded.push_back(kHud + kHudMobys + (kHudCounterMoby + i) * kMobyBytes);
  }
}

// Every state from 1 to 4 copies the crystal fragments that are still on screen into the shaded
// list, walking the level array itself rather than any queue built earlier.
Status visibleFragments(Core *core, std::vector<uint32_t> &shaded) {
  const uint32_t first = core->mem_r32(kLevelMobys);
  for (uint32_t i = 0, moby = first; i < kMaxLevelMobys; ++i, moby += kMobyBytes) {
    const uint32_t state = core->mem_r8(moby + kMobyState);
    if (state == 0xffu) {
      return Status::Ready;
    }
    if ((uint32_t)core->mem_r16(moby + kMobyClass) == kFragmentClass && state < kVisibleState) {
      if (shaded.size() >= kShadedCapacity) {
        return Status::ListCapacityExceeded;
      }
      shaded.push_back(moby);
    }
  }
  return Status::UnterminatedMobyArray;
}

void tail(const State &state, std::vector<Producer> &producers, bool withFade) {
  producers.push_back(Producer::Environment);
  producers.push_back(Producer::Cyclorama);
  producers.push_back(Producer::Particles);
  if (withFade && state.fade != 0) {
    producers.push_back(Producer::ScreenFade);
  }
  if (state.borderEnabled) {
    producers.push_back(Producer::ScreenBorder);
  }
}

} // namespace

State read(Core *core) {
  State state{};
  if (core == nullptr) {
    return state;
  }
  state.state = core->mem_r32(kCutscene + kState);
  state.ticks = (int32_t)core->mem_r32(kCutscene + kTicks);
  state.fade = (int32_t)core->mem_r32(kCutscene + kFade);
  state.cutsceneSpyro = core->mem_r32(kCutscene + kCutsceneSpyro);
  state.cutsceneDragon = core->mem_r32(kCutscene + kCutsceneDragon);
  state.rescuedDragon = core->mem_r32(kCutscene + kRescuedDragon);
  state.borderEnabled =
      core->mem_r32(kScreenBorderEnabled) != 0u || core->mem_r32(kBorderOverride) != 0u;
  state.burstActive = core->mem_r32(kBurstGate) != 0u;
  return state;
}

Plan plan(Core *core, const State &state) {
  Plan plan{};
  if (core == nullptr) {
    return plan;
  }
  plan.status = Status::Ready;
  auto &producers = plan.producers;
  if (state.state == 0) {
    // The only state that draws the world the way FIELD does, through the whole 0x80019698 chain.
    producers.push_back(Producer::QueueMobys);
    producers.push_back(Producer::FieldChain);
    tail(state, producers, true);
    return plan;
  }

  plan.lists.writeDraw = true;
  plan.lists.writeShaded = true;
  plan.explicitDrawSource = true;
  if (state.state < 4) {
    // The rescued dragon leaves the draw list as soon as its own state says it is gone; the
    // cutscene Spyro only joins for the last of the three.
    if (state.rescuedDragon != 0u &&
        core->mem_r8(state.rescuedDragon + kMobyState) < kVisibleState) {
      plan.lists.draw.push_back(state.rescuedDragon);
    }
    if (state.state == 3 && state.cutsceneSpyro != 0u) {
      plan.lists.draw.push_back(state.cutsceneSpyro);
    }
    plan.status = visibleFragments(core, plan.lists.shaded);
    if (plan.status != Status::Ready) {
      return plan;
    }
    producers.push_back(Producer::RescuedText);
    producers.push_back(Producer::CopyHudMobys);
    producers.push_back(Producer::Regular);
    producers.push_back(Producer::ClearDrawList);
    producers.push_back(Producer::Shaded);
    producers.push_back(Producer::MobyShadows);
    producers.push_back(Producer::SpyroModel);
    producers.push_back(Producer::SpyroShadow);
    tail(state, producers, true);
    return plan;
  }
  if (state.state == 4) {
    if (state.cutsceneSpyro != 0u) {
      plan.lists.draw.push_back(state.cutsceneSpyro);
    }
    if (state.cutsceneDragon != 0u) {
      plan.lists.draw.push_back(state.cutsceneDragon);
    }
    plan.status = visibleFragments(core, plan.lists.shaded);
    if (plan.status != Status::Ready) {
      return plan;
    }
    if (state.ticks < kRescuedTextTicks) {
      producers.push_back(Producer::RescuedText);
      producers.push_back(Producer::CopyHudMobys);
    }
    producers.push_back(Producer::Regular);
    producers.push_back(Producer::ClearDrawList);
    producers.push_back(Producer::Shaded);
    producers.push_back(Producer::MobyShadows);
    // The distinguishing absence: state 4 draws Spyro's shadow but NOT his model, because the
    // cutscene Spyro in the draw list above is the one on screen.
    producers.push_back(Producer::SpyroShadow);
    tail(state, producers, false);
    return plan;
  }
  if (state.state == 5) {
    if (state.cutsceneSpyro != 0u) {
      plan.lists.draw.push_back(state.cutsceneSpyro);
    }
    hudCounters(plan.lists.shaded);
    producers.push_back(Producer::CameraShake);
    producers.push_back(Producer::Regular);
    producers.push_back(Producer::ClearDrawList);
    producers.push_back(Producer::Shaded);
    producers.push_back(Producer::SpyroModel);
    producers.push_back(Producer::SpyroShadow);
    tail(state, producers, false);
    return plan;
  }
  if (state.state == 6) {
    // No preparation and no regular pass at all, so the shadow list keeps whatever the previous
    // state staged. Reproduced rather than repaired: the shaded pass is the only consumer here.
    plan.lists.writeDraw = false;
    plan.explicitDrawSource = false;
    hudCounters(plan.lists.shaded);
    producers.push_back(Producer::ClearDrawList);
    producers.push_back(Producer::Shaded);
    producers.push_back(Producer::SpyroModel);
    producers.push_back(Producer::SpyroShadow);
    tail(state, producers, false);
    return plan;
  }
  if (state.state == 7) {
    if (state.ticks < kFinalShadedOnlyTicks) {
      plan.lists.writeDraw = false;
      plan.explicitDrawSource = false;
      hudCounters(plan.lists.shaded);
      producers.push_back(Producer::ClearDrawList);
      producers.push_back(Producer::Shaded);
    } else {
      // The one state that goes back to the level-array classification, and the only one besides
      // state 0 that reaches the secondary pass.
      plan.lists.writeDraw = false;
      plan.lists.writeShaded = false;
      plan.explicitDrawSource = false;
      producers.push_back(Producer::QueueMobys);
      producers.push_back(Producer::Regular);
      producers.push_back(Producer::ClearDrawList);
      producers.push_back(Producer::Secondary);
      producers.push_back(Producer::Shaded);
      producers.push_back(Producer::MobyShadows);
    }
    producers.push_back(Producer::SpyroModel);
    producers.push_back(Producer::SpyroShadow);
    tail(state, producers, false);
    if (state.fade != 0) {
      // State 7 is the one branch that fades AFTER the border rather than before it.
      producers.push_back(Producer::ScreenFade);
    }
    return plan;
  }
  // 0x8001CFDC has no branch for a state past 7, so it would draw nothing at all. Name it rather
  // than presenting an empty frame that looks like a composition.
  plan.status = Status::UnknownState;
  plan.producers.clear();
  return plan;
}

void commit(Core *core, const Plan &plan) {
  if (core == nullptr) {
    return;
  }
  if (plan.lists.writeDraw) {
    for (uint32_t i = 0; i < plan.lists.draw.size(); ++i) {
      core->mem_w32(kDrawList + i * 4u, plan.lists.draw[i]);
    }
    core->mem_w32(kDrawList + (uint32_t)plan.lists.draw.size() * 4u, 0u);
  }
  if (plan.lists.writeShaded) {
    for (uint32_t i = 0; i < plan.lists.shaded.size(); ++i) {
      core->mem_w32(kShadedList + i * 4u, plan.lists.shaded[i]);
    }
    core->mem_w32(kShadedList + (uint32_t)plan.lists.shaded.size() * 4u, 0u);
  }
}

void clearDrawList(Core *core) {
  if (core == nullptr) {
    return;
  }
  for (uint32_t offset = 0; offset < kDrawListBytes; offset += 4u) {
    core->mem_w32(kDrawList + offset, 0u);
  }
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidCore:
    return "invalid-core";
  case Status::UnterminatedMobyArray:
    return "unterminated-moby-array";
  case Status::ListCapacityExceeded:
    return "list-capacity-exceeded";
  case Status::UnknownState:
    return "unknown-state";
  }
  return "unknown";
}

const char *producerName(Producer producer) {
  switch (producer) {
  case Producer::QueueMobys:
    return "0x800521C0";
  case Producer::RescuedText:
    return "0x80018728";
  case Producer::CopyHudMobys:
    return "0x80018880";
  case Producer::CameraShake:
    return "camera-shake";
  case Producer::ClearDrawList:
    return "clear-draw-list";
  case Producer::Regular:
    return "0x8001F798";
  case Producer::Secondary:
    return "0x80020F34";
  case Producer::Shaded:
    return "0x80022A2C";
  case Producer::MobyShadows:
    return "0x80059F8C";
  case Producer::SpyroModel:
    return "0x80023AC4";
  case Producer::SpyroShadow:
    return "0x80059A48";
  case Producer::Environment:
    return "0x8002B9CC";
  case Producer::Cyclorama:
    return "0x80050BD0";
  case Producer::Particles:
    return "0x800573C8";
  case Producer::ScreenFade:
    return "0x800190D4";
  case Producer::ScreenBorder:
    return "0x80018F30";
  case Producer::FieldChain:
    return "0x80019698";
  }
  return "unknown";
}

} // namespace spyro::dragon_scene
