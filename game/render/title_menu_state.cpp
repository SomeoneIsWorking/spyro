#include "title_menu_state.h"

#include "core.h"

namespace spyro::title_menu_state {
namespace {

constexpr uint32_t kAnim = 0x80078D84u;
constexpr uint32_t kMode2State = 0x80078D7Cu;
constexpr uint32_t kPage = 0x80078D88u;
constexpr uint32_t kOption = 0x80078D8Cu;
constexpr uint32_t kSecondaryOption = 0x80078D90u;
constexpr uint32_t kCard = 0x80078DA0u;
constexpr uint32_t kSaveFilePointers = 0x80078DC8u;
constexpr uint32_t kSaveFileVisited = 0x40u;
constexpr uint32_t kSaveFileDragons = 0x88u;
constexpr uint32_t kLevelCount = 36u;
constexpr uint32_t kEaseSlideOut = 0x8006FA84u;
constexpr int32_t kSlideYBias = 119;
constexpr uint32_t kGateVarPtr = 0x80075680u;
constexpr uint32_t kGateValue = 0x492u;

} // namespace

title_menu_recipe::Mode1Input State::mode1Input() const {
  return {.substate = page,
          .optionSelected = optionSelected,
          .subTick = anim,
          .cardSelected = cardSelected};
}

title_menu_recipe::Mode2Input State::mode2Input() const {
  return {.state = mode2State,
          .optionSelected = optionSelected,
          .subTick = anim,
          .secondaryOption = secondaryOption,
          .cardSelected = cardSelected,
          .slideY = mode2SlideY,
          .slots = mode2Slots};
}

State read(Core *core) {
  const uint32_t gatePtr = core->mem_r32(kGateVarPtr);
  State state = {.mode = core->mem_r32(kModeAddress),
                 .mode2State = core->mem_r32(kMode2State),
                 .page = core->mem_r32(kPage),
                 .anim = core->mem_r32(kAnim),
                 .optionSelected = core->mem_r32(kOption),
                 .secondaryOption = core->mem_r32(kSecondaryOption),
                 .cardSelected = static_cast<int32_t>(core->mem_r32(kCard)),
                 .gateOpen = gatePtr != 0u && core->mem_r32(gatePtr) >= kGateValue};

  if (state.mode == 2u && state.mode2State > 0u && state.mode2State < 5u) {
    for (size_t i = 0; i < state.mode2Slots.size(); ++i) {
      const uint32_t save = core->mem_r32(kSaveFilePointers + static_cast<uint32_t>(i) * 4u);
      auto &slot = state.mode2Slots[i];
      slot.occupied = core->mem_r8(save + kSaveFileVisited) != 0u;
      if (!slot.occupied) {
        continue;
      }
      slot.homeworldSprite = static_cast<int32_t>(core->mem_r8(save) / 10u) + 1;
      for (uint32_t level = 0; level < kLevelCount; ++level) {
        slot.dragonCount += core->mem_r8(save + kSaveFileDragons + level);
      }
    }
  } else if (state.mode == 2u && state.mode2State >= 5u) {
    state.mode2SlideY =
        static_cast<int32_t>(core->mem_r8(kEaseSlideOut + state.anim)) - kSlideYBias;
  }
  return state;
}

} // namespace spyro::title_menu_state
