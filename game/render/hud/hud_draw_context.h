// hud_draw_context.h — which screen-space widget is being drawn right now, for emitters that cannot
// tell for themselves.
//
// A HUD widget's anchor class is a property of the widget, but the guest reaches its shared
// emitters from several widget drawers: on Spyro 3 the collectable counter (element 0x80067248) and
// the egg counter (element 0x800672F0) are both drawn by FUN_80029904, so the return address
// cannot name which widget is on screen.
//
// The element is therefore carried DOWN one level, from the drawer that knows it to the emitters
// that need it: one word, published by the drawer override before it runs the original and cleared
// after, read by the emitter overrides while it is set.
//
// Scoped to one drawer invocation, and per Core, so two Cores in one process cannot answer for each
// other. Zero means "no widget in progress".
#pragma once

#include <cstdint>

namespace spyro::hud_draw_context {

// The widget element whose draw is in progress, published by that widget's drawer override.
//
// `FUN_800291B8` (the value emitter) does not draw its digits itself: it calls the icon emitter
// `FUN_800289C8` once per glyph from 0x80029374. So an icon call made while a value emitter is
// running belongs to a value that has already been corrected, and must not be corrected again.
struct Draw {
  void begin(std::uint32_t widgetElement) {
    element = widgetElement;
  }

  void end() {
    element = 0;
    inValue = false;
  }

  [[nodiscard]] std::uint32_t current() const {
    return element;
  }

  void beginValue() {
    inValue = true;
  }

  void endValue() {
    inValue = false;
  }

  // True between a widget's value emitter being entered and its return.
  [[nodiscard]] bool insideValue() const {
    return inValue;
  }

private:
  std::uint32_t element = 0;
  bool inValue = false;
};

} // namespace spyro::hud_draw_context