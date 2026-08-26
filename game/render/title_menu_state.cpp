#include "title_menu_state.h"

#include "core.h"

namespace spyro::title_menu_state {
namespace {

constexpr uint32_t kAnim = 0x80078D84u;
constexpr uint32_t kPage = 0x80078D88u;
constexpr uint32_t kOption = 0x80078D8Cu;
constexpr uint32_t kCard = 0x80078DA0u;
constexpr uint32_t kGateVarPtr = 0x80075680u;
constexpr uint32_t kGateValue = 0x492u;

} // namespace

title_menu_recipe::Mode1Input State::mode1Input() const {
  return {.substate = page,
          .optionSelected = optionSelected,
          .subTick = anim,
          .cardSelected = cardSelected};
}

State read(Core *core) {
  const uint32_t gatePtr = core->mem_r32(kGateVarPtr);
  return {.mode = core->mem_r32(kModeAddress),
          .page = core->mem_r32(kPage),
          .anim = core->mem_r32(kAnim),
          .optionSelected = core->mem_r32(kOption),
          .cardSelected = static_cast<int32_t>(core->mem_r32(kCard)),
          .gateOpen = gatePtr != 0u && core->mem_r32(gatePtr) >= kGateValue};
}

} // namespace spyro::title_menu_state
