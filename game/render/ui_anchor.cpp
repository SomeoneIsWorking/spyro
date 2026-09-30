#include "ui_anchor.h"

#include "wide_clip_plan.h"
#include "wide_screen_space.h"

#include <lucent/log.h>

namespace spyro::ui_anchor {
namespace {

const char *channel() {
  return "uihud";
}

} // namespace

// The frame this policy relates, from the ONE place that knows the two widths.
Frame frame(Core *core) {
  return Frame{.authored = wide::kNativeClipWidth, .drawn = wide_screen_space::drawClipRight(core)};
}

Placed placeAndReport(const Element &element,
                      Anchor anchor,
                      std::int32_t authoredX,
                      std::int32_t authoredWidth,
                      const Frame &frame) {
  const Placed placed = place(anchor, authoredX, authoredWidth, frame);
  if (placed.refusal != Refusal::None) {
    // A refused element is a fact about the PORT's inputs, not about the picture, so it is reported
    // with the reason and the values it refused. Silently drawing it unshifted would be the exact
    // class of quiet wrong answer this project keeps finding.
    lucent::warn(channel(),
                 "REFUSED element={} index={} anchor={} authored=({}, {}) reason={} "
                 "frame={}->{}",
                 element.name,
                 element.index,
                 name(anchor),
                 authoredX,
                 authoredWidth,
                 refusalName(placed.refusal),
                 frame.authored,
                 frame.drawn);
    return placed;
  }
  lucent::debug(channel(),
                "element={} index={} anchor={} authored=({}, {}) drawn=({}, {}) frame={}->{} "
                "offset={}",
                element.name,
                element.index,
                name(anchor),
                authoredX,
                authoredWidth,
                placed.box.x,
                placed.box.width,
                frame.authored,
                frame.drawn,
                placed.offset);
  return placed;
}

std::int32_t correctionAndReport(const Element &element, Anchor anchor, const Frame &frame) {
  const auto shift = correction(anchor, frame);
  if (!shift) {
    lucent::warn(channel(),
                 "REFUSED element={} index={} anchor={} reason={} frame={}->{}",
                 element.name,
                 element.index,
                 name(anchor),
                 refusalName(refusalFor(anchor, frame)),
                 frame.authored,
                 frame.drawn);
    return 0;
  }
  lucent::debug(channel(),
                "element={} index={} anchor={} correction={} frame={}->{}",
                element.name,
                element.index,
                name(anchor),
                *shift,
                frame.authored,
                frame.drawn);
  return *shift;
}

} // namespace spyro::ui_anchor
