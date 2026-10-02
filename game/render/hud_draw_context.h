// hud_draw_context.h — which screen-space widget is being drawn RIGHT NOW, for emitters that cannot
// tell for themselves.
//
// WHY THIS EXISTS. A HUD widget's anchor class is a property of the WIDGET — a gem counter authored
// against the left edge stays against the left edge, an egg counter against the right one, a lives
// counter stays centred — but the guest reaches its shared emitters from several widget drawers. On
// Spyro 3 the collectable counter (element 0x80067248, authored x 20) and the egg counter
// (element 0x800672F0, authored x 492) are both drawn by the SAME drawer, FUN_80029904, and both go
// through the SAME two call sites into the icon emitter 0x800289C8 and the value emitter
// 0x800291B8. The return address therefore cannot name which widget is on screen: measured, both
// counters arrive at ra 0x800299B8 with x 20 and x 382 respectively.
//
// So the element is carried DOWN one level, from the drawer that knows it to the emitters that need
// it. That is the whole of this owner: one word, published by the drawer override before it runs
// the original and cleared after, read by the emitter overrides while it is set.
//
// LIFETIME AND SCOPE, stated because a "current widget" that outlives its widget would be a class
// applied to the wrong element. It is set and cleared inside ONE drawer invocation, the guest is
// single-threaded, and the drawers make their emitter calls with nothing between, so every read of
// a non-zero element is inside the draw that published it. Per Core, so two Cores in one process
// cannot answer for each other. Zero means "no widget in progress", which is the identity
// correction.
#pragma once

#include <cstdint>

namespace spyro::hud_draw_context {

// The widget element whose draw is in progress, published by that widget's drawer override.
// WHY THE VALUE EMITTER NEEDS A SCOPE OF ITS OWN. The two emitters are not independent. On this
// title the widget drawer emits an ICON and then a VALUE, but the VALUE emitter does not draw its
// digits itself: `FUN_800291B8` calls the ICON emitter `FUN_800289C8` once per glyph from
// `0x80029374` (return `0x80029378`). So correcting the value emitter's X puts the whole digit run
// in the right place, and then the icon emitter is reached again from INSIDE it, for glyphs whose X
// already carries that correction. Correcting those again is a double correction, and it is
// measured: for the collectable counter the digit lands at x = -111 instead of -25 and disappears
// off the left edge, and for the egg counter the icon lands at 595 instead of 468 and is jammed
// against the right edge, which is what reverses the icon/count order the player reads. So an icon
// call made while a value emitter is running belongs to a value that has ALREADY been corrected,
// and must not be corrected again. This flag is that statement, and it is scoped to the value
// emitter's own body in the same way the element is scoped to the drawer's.
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