#pragma once

#include "title_menu_recipe.h"

#include <cstdint>

class Core;

namespace spyro::title_menu_state {

inline constexpr uint32_t kModeAddress = 0x80078D78u;

struct State {
  uint32_t mode = 0;
  uint32_t page = 0;
  uint32_t anim = 0;
  uint32_t optionSelected = 0;
  int32_t cardSelected = 0;
  bool gateOpen = false;

  title_menu_recipe::Mode1Input mode1Input() const;
};

// One guest-memory lens over the title overlay's state block. Native
// presentation and the retained-body oracle must not carry separate address
// maps for the same fields.
State read(Core *core);

} // namespace spyro::title_menu_state
