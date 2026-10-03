#include "spyro2_hud_anchor.h"

#include "core.h"
#include "native_execution.h"
#include "spyro2_widescreen_facts.h"
#include "ui_anchor.h"

namespace spyro2::hud_anchor {
namespace {

// The frame this title's HUD anchors against, from the ONE shared rule: this Core's own widening
// decision, or the title's authored window when it is not widening. Equal widths before the plan
// latches and whenever it does not widen, which makes every correction below zero.
spyro::ui_anchor::Frame frame(Core &core) {
  return spyro::ui_anchor::widescreenFrame(core, spyro2::kWidescreenFacts.nativeWidth);
}

void shiftArgument(Core &core, std::uint32_t reg, std::int32_t correction) {
  core.r[reg] = static_cast<std::uint32_t>(static_cast<std::int32_t>(core.r[reg]) + correction);
}

// FUN_8005251C(element, image, x, y): the counter's x is $a2.
void counterDrawer(Core *core) {
  if (const auto anchor = counterAnchor(core->r[4])) {
    shiftArgument(*core,
                  6,
                  spyro::ui_anchor::correctionAndReport(
                      {.name = "hud-counter", .index = core->r[4]}, *anchor, frame(*core)));
  }
  (void)spyro::callOriginalOrPropagate(*core, kCounterDrawer);
}

// FUN_800520CC(image, x, y, size): the emitted x is $a1.
void spriteEmitter(Core *core) {
  if (const auto anchor = emitterAnchor(core->r[31])) {
    shiftArgument(*core,
                  5,
                  spyro::ui_anchor::correctionAndReport(
                      {.name = "hud-meter", .index = core->r[31]}, *anchor, frame(*core)));
  }
  (void)spyro::callOriginalOrPropagate(*core, kSpriteEmitter);
}

} // namespace

void registerOverrides(Core &core) {
  spyro::installNativeOverride(core, kCounterDrawer, "spyro2-hud-counter", counterDrawer);
  spyro::installNativeOverride(core, kSpriteEmitter, "spyro2-hud-sprite", spriteEmitter);
}

} // namespace spyro2::hud_anchor
