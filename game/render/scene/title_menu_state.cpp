#include "title_menu_state.h"
#include "guest_globals.h"

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
// g_CutsceneLayout -- the pointer global, NOT a value. Recovered from SCUS_942.28: exactly one
// `lui $rX,0x8007` + `sw 0x5680($rX)` writer in the whole main image, at 0x80014A38, and seventeen
// `lui`+`lw` reads as a pointer base (loaders.c:955 is its only writer in the decompilation). So
// the word AT this address is a pointer, and the value this state gates on is the first int through
// it.
constexpr uint32_t kCutsceneLayout = 0x80075680u;
constexpr uint32_t kCutsceneCurrentTick = 0u;
// CutsceneLayout.m_CurrentTick >= 1170 (0x492) -- RETAIL'S OWN logo gate, from the
// `m_CurrentTick < 1100` / `>= 1169` tests in overlays/titlescreen.c:100-157. This was
// `kGateValue`, which named a number without saying whose decision it was; two constants in this
// workspace were confused with an unrelated gameplay word before they were ever measured, so the
// threshold is stated as retail's and not as a bare literal.
constexpr uint32_t kTitleLogoTick = 1170u;

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
  const uint32_t cutsceneLayout = core->mem_r32(kCutsceneLayout);
  State state = {.mode = core->mem_r32(spyro::guest::kTitlescreenState),
                 .mode2State = core->mem_r32(kMode2State),
                 .page = core->mem_r32(kPage),
                 .anim = core->mem_r32(kAnim),
                 .optionSelected = core->mem_r32(kOption),
                 .secondaryOption = core->mem_r32(kSecondaryOption),
                 .cardSelected = static_cast<int32_t>(core->mem_r32(kCard)),
                 .gateOpen =
                     cutsceneLayout != 0u &&
                     core->mem_r32(cutsceneLayout + kCutsceneCurrentTick) >= kTitleLogoTick};

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
