// menu_panel_submit.h — the translucent box and lit outline the guest's menu handlers draw.
//
// The pause menu (0x8001A40C) and the fairy dialogue (0x8001D718, through func_8001860C) both draw
// a semi-transparent GP0 0x2A quad and 0x8001844C gradient lines over the world, differing only in
// colour and rectangle. Callers hand over rectangles in the guest's offset-relative coordinates
// plus the horizontal `shift` their anchoring owner decided.
#pragma once

#include "pause_menu_recipe.h"

#include <cstdint>
#include <span>

class Core;
struct RenderQueue;

namespace spyro::menu_panel {

struct Colour {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// One semi-transparent flat quad covering `placed`.
void submitPanel(Core *core,
                 RenderQueue &queue,
                 const pause_menu::PanelRect &authored,
                 Colour colour,
                 std::int32_t shift);

// The outline: one shaded line per segment. A segment that would fall outside the active draw area
// (`drawAreaX1` is its exclusive right edge) is skipped against the real clip, not a literal.
void submitBorder(Core *core,
                  RenderQueue &queue,
                  std::span<const pause_menu::Segment> authored,
                  std::int32_t drawAreaX1,
                  std::int32_t shift);

} // namespace spyro::menu_panel
