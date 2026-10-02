#include "spyro2_hud_anchor.h"

#include "core.h"
#include "guest_widescreen_math.h"
#include "guest_widescreen_owner.h"
#include "native_execution.h"

namespace spyro2::hud_anchor {
namespace {

// The guest's authored frame and the presented one, from this title's own widening decision (the
// shared owner in guest_widescreen_owner.h). Equal before the plan latches and whenever it does not
// widen, which makes every correction zero -- and the authored width is the title's own fact rather
// than a constant, so a HUD anchored against the wrong window would be a compile error here rather
// than a misplaced element in a capture.
//
// A Core whose projection leaves are not bound has no owner and is not widening: both frame widths
// are then the title's authored window and every correction below is zero, which is what an
// unlatched owner already returns.
spyro::ui_anchor::Frame frame(Core &core) {
  const spyro::GuestWidescreenOwner *widescreen = spyro::GuestWidescreenOwner::of(core);
  if (widescreen == nullptr || !widescreen->latched() || !widescreen->plan().widescreen()) {
    const auto native = widescreen == nullptr ? 0 : widescreen->facts().nativeWidth;
    return {.authored = native, .drawn = native};
  }
  const GuestProjectionPlan &plan = widescreen->plan();
  return {.authored = plan.nativeExtent.width, .drawn = plan.presentationExtent.width};
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
