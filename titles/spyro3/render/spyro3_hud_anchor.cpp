#include "spyro3_hud_anchor.h"

#include "core.h"
#include "hud_draw_context.h"
#include "native_execution.h"
#include "spyro3_widescreen_facts.h"
#include "spyro_context.h"
#include "ui_anchor.h"

#include <cstdint>

namespace spyro3::hud_anchor {
namespace {

// The frame this title's HUD anchors against, from the ONE shared rule: this Core's own widening
// decision, or the title's authored window when it is not widening, which makes every correction
// zero rather than a guess.
spyro::ui_anchor::Frame frame(Core &core) {
  return spyro::ui_anchor::widescreenFrame(core, spyro3::kWidescreenFacts.nativeWidth);
}

// Shift the X argument of an emitter ($a1) by the widget's anchor class, if the widget in progress
// has one. The element is the drawer's own argument and is never written; only this register moves.
void shiftEmitterArgument(Core &core) {
  const std::uint32_t element = spyro::context(core).hudDraw.current();
  if (const auto anchor = counterAnchor(element)) {
    core.r[5] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(core.r[5]) +
        spyro::ui_anchor::correctionAndReport(
            {.name = "hud-counter", .index = element}, *anchor, frame(core)));
  }
}

// A widget drawer publishes its element for the emitters it is about to call and clears it after,
// so every emitter call inside the draw reads this widget and every call outside reads none. Both
// drawers are the same body: publish, run the original unchanged, clear.
void counterDrawer(Core *core) {
  spyro::context(*core).hudDraw.begin(core->r[4]);
  (void)spyro::callOriginalOrPropagate(*core, kCounterDrawer);
  spyro::context(*core).hudDraw.end();
}

void livesDrawer(Core *core) {
  spyro::context(*core).hudDraw.begin(core->r[4]);
  (void)spyro::callOriginalOrPropagate(*core, kLivesDrawer);
  spyro::context(*core).hudDraw.end();
}

// A widget's OWN icon is what its anchor class moves. The same emitter is also called from INSIDE
// the value emitter, once per digit glyph, and those calls are not this widget's icon to move: see
// the value emitter below.
void iconEmitter(Core *core) {
  if (!spyro::context(*core).hudDraw.insideValue()) {
    shiftEmitterArgument(*core);
  }
  (void)spyro::callOriginalOrPropagate(*core, kIconEmitter);
}

// A widget's value. This emitter draws its digits by CALLING the icon emitter once per glyph, so it
// opens a scope of its own: the glyph Xs it hands down already carry the correction applied here,
// and the icon emitter must leave them alone. Without that scope the digits are corrected TWICE —
// measured as the collectable count landing at x -111 instead of -25, off the left edge, so the
// number is not drawn at all; and the egg's icon landing at 595 instead of 468, jammed against the
// right edge, which reads as the icon and the count having swapped sides.
void valueEmitter(Core *core) {
  shiftEmitterArgument(*core);
  spyro::context(*core).hudDraw.beginValue();
  (void)spyro::callOriginalOrPropagate(*core, kValueEmitter);
  spyro::context(*core).hudDraw.endValue();
}

} // namespace

void registerOverrides(Core &core) {
  spyro::installNativeOverride(core, kCounterDrawer, "spyro3-hud-counter", counterDrawer);
  spyro::installNativeOverride(core, kLivesDrawer, "spyro3-hud-lives", livesDrawer);
  spyro::installNativeOverride(core, kIconEmitter, "spyro3-hud-icon", iconEmitter);
  spyro::installNativeOverride(core, kValueEmitter, "spyro3-hud-value", valueEmitter);
}

} // namespace spyro3::hud_anchor