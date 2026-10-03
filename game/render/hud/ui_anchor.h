// ui_anchor.h — THE ONE horizontal anchoring policy for Spyro 1's screen-space UI.
//
// WHY THIS EXISTS. Widescreen widens the canvas from the guest's 512 columns to 684. Everything in
// the picture that is a PROJECTION moves by itself, because the projection's horizontal centre
// moved; nothing that is a FIXED 4:3 layout moves by itself, because the guest authored it against
// 512 and this port has no reason to know. The framework's queue answered that for every 2D
// producer with one rule — "authored 4:3 x is CENTRED in the wide frame" (RQ_2D_AUTHORED_4_3) — and
// centring is the right answer for exactly one class of element. It is the wrong answer for the
// rest, and it is wrong in a way no capture of a 3D scene can show:
//
//   * an EDGE element (the life orbs, a corner icon) authored 40 px from the left edge of a
//     512-wide frame is 40 px from the edge of a 684-wide frame too. Centring it slides it 86 px
//     INTO the picture, where it stops being a corner element and starts floating over the world
//     the widening just revealed. The authored EDGE DISTANCE is the thing the player sees, and
//     centring destroys it;
//   * a RIGHT-edge element has to move by the FULL margin (172 px), not by half, or its distance
//     from the right edge doubles;
//   * a uniform FILL (the screen fade, the screen border) is not an anchored element at all: it
//     spans the frame it is drawn into, which is what the fade and border recipes already do by
//     taking the live render width.
//
// The second reason this is one owner and not a per-producer constant: the arithmetic is three
// lines, and three lines copied into five producers is five places to be wrong and no place that
// can be tested. The class is a fact about the ELEMENT, so it is declared where the element is
// authored, and the arithmetic is here.
//
// IT IS A PRESENTATION CHANGE AND NOTHING ELSE. `place` and `correction` take an authored x and
// return a drawn x; neither reads nor writes guest memory, and neither is allowed to. Widescreen
// must not decide what the guest believes (issue 0152, `game/core/wide_clip_plan.h`), and the HUD
// arena's Moby records are guest memory: their positions stay exactly as the guest wrote them at
// both aspects, and the anchor is applied to the port's own draw record on the way to the
// rasterizer. At 4:3 both entry points are the identity, so the 4:3 picture is unchanged by
// construction rather than by a saved copy.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

struct Core;

namespace spyro::ui_anchor {

// The class of element. It is a property of the LAYOUT, never of the current aspect: an element
// that is edge-anchored at 4:3 is edge-anchored at 16:9, and asking the aspect is what produces a
// HUD that migrates across the screen when the setting changes.
enum class Anchor : std::uint8_t {
  // Authored distance to the LEFT edge is preserved: the drawn x equals the authored x, so the
  // element keeps the same inset from the widened frame's left edge.
  LeftEdge = 0,
  // The authored box keeps its distance to the frame's CENTRE, which is the framework's existing
  // RQ_2D_AUTHORED_4_3 rule and the right answer for a panel, a menu or a caption block that the
  // guest laid out around a centre.
  Centred = 1,
  // Authored distance to the RIGHT edge is preserved: the element moves by the FULL margin, twice
  // what a centred one does.
  RightEdge = 2,
};

inline const char *name(Anchor anchor) {
  switch (anchor) {
  case Anchor::LeftEdge:
    return "left-edge";
  case Anchor::Centred:
    return "centred";
  case Anchor::RightEdge:
    return "right-edge";
  }
  return "unknown";
}

// The two frame widths this policy relates: the width the guest authored its layout against, and
// the width this port presents into. They are equal at 4:3, which is what makes every answer below
// the identity there.
struct Frame {
  std::int32_t authored = 0;
  std::int32_t drawn = 0;
};

// Why an authored box was refused. Every refusal is a value the port got wrong — a negative or zero
// size, a frame that is NARROWER than the one the guest authored (which is not widescreen, it is a
// misread of the width), or a box that does not fit the authored frame (an element authored
// outside its own layout is not something to move silently). A refused element is left where the
// guest put it.
enum class Refusal : std::uint8_t {
  None = 0,
  NonPositiveWidth,
  NarrowerFrame,
  OutsideAuthoredFrame,
  UnknownClass,
};

inline const char *refusalName(Refusal refusal) {
  switch (refusal) {
  case Refusal::None:
    return "none";
  case Refusal::NonPositiveWidth:
    return "non-positive-width";
  case Refusal::NarrowerFrame:
    return "narrower-frame";
  case Refusal::OutsideAuthoredFrame:
    return "outside-authored-frame";
  case Refusal::UnknownClass:
    return "unknown-class";
  }
  return "unknown";
}

// A placed box, in the DRAWN frame. `x` is inclusive-left like every guest RECT in this port, and
// `width` is the AUTHORED width in every class: nothing is stretched, so a glyph is the
// same number of pixels wide at 16:9 as at 4:3 and only its origin moves.
struct Box {
  std::int32_t x = 0;
  std::int32_t width = 0;
};

struct Placed {
  Box box{};
  // The horizontal displacement applied to the authored x. Zero at 4:3 for every class, which is
  // the property the 4:3 identity rests on.
  std::int32_t offset = 0;
  Refusal refusal = Refusal::None;
};

constexpr bool known(Anchor anchor) {
  return anchor == Anchor::LeftEdge || anchor == Anchor::Centred || anchor == Anchor::RightEdge;
}

// The frame itself is checked before anything is placed, because a "widened" frame that is not
// wider is a misread width and every offset derived from it would be silently wrong.
constexpr Refusal checkFrame(const Frame &frame) {
  if (frame.authored <= 0 || frame.drawn <= 0) {
    return Refusal::NonPositiveWidth;
  }
  if (frame.drawn < frame.authored) {
    return Refusal::NarrowerFrame;
  }
  return Refusal::None;
}

// Why an element of this class cannot be anchored into this frame, or None.
constexpr Refusal refusalFor(Anchor anchor, const Frame &frame) {
  return known(anchor) ? checkFrame(frame) : Refusal::UnknownClass;
}

// The one horizontal margin: how much wider the drawn frame is than the authored one, halved. At
// 4:3 it is zero, and it is the unit the whole policy is written in — a centred element moves by
// one margin, a right-edge element by two.
constexpr std::int32_t margin(const Frame &frame) {
  return (frame.drawn - frame.authored) / 2;
}

// How far an anchored element's ORIGIN moves, in the drawn frame. `Centred` moves by the margin,
// `LeftEdge` not at all, `RightEdge` by the whole widening.
constexpr std::optional<std::int32_t> offset(Anchor anchor, const Frame &frame) {
  if (refusalFor(anchor, frame) != Refusal::None) {
    return std::nullopt;
  }
  switch (anchor) {
  case Anchor::LeftEdge:
    return 0;
  case Anchor::Centred:
    return margin(frame);
  case Anchor::RightEdge:
    return frame.drawn - frame.authored;
  }
  return std::nullopt;
}

// The correction to apply to an x that has ALREADY been shifted by the framework's centring rule.
//
// This is the entry point for a producer whose x did not come from an authored constant but out of
// the widened projection — the HUD glyphs, which the guest positions in SCREEN SPACE and which
// therefore inherit the projection's own +margin. Applying `margin` to those would move them a
// second time, so what they need is the DIFFERENCE between their class and the class the
// projection already applied. At 4:3 both are zero and the correction is the identity; at 16:9 a
// left-edge glyph gets `-margin` (back to its authored inset from the new left edge), a centred one
// gets 0 (it is already centred, which is correct), and a right-edge one gets `+margin`.
//
// It is a correction and not a replacement precisely so that the two halves of the same HUD — the
// sprites the guest authored as RECTs and the glyphs the guest authored through the shaded queue —
// agree with each other by construction instead of by coincidence.
constexpr std::optional<std::int32_t> correction(Anchor anchor, const Frame &frame) {
  const std::optional<std::int32_t> own = offset(anchor, frame);
  if (!own) {
    return std::nullopt;
  }
  return *own - margin(frame);
}

// The placed box for an authored element. `authoredX`/`authoredWidth` are the guest's own values;
// the returned x is in the drawn frame and the returned width is the authored one, because an
// anchored element is MOVED, never resized.
constexpr Placed
place(Anchor anchor, std::int32_t authoredX, std::int32_t authoredWidth, const Frame &frame) {
  Placed placed;
  if (const Refusal refusal = refusalFor(anchor, frame); refusal != Refusal::None) {
    placed.refusal = refusal;
    return placed;
  }
  if (authoredWidth <= 0) {
    placed.refusal = Refusal::NonPositiveWidth;
    return placed;
  }
  if (authoredX < 0 || authoredX + authoredWidth > frame.authored) {
    placed.refusal = Refusal::OutsideAuthoredFrame;
    return placed;
  }
  const std::int32_t shift = *offset(anchor, frame);
  placed.box = {authoredX + shift, authoredWidth};
  placed.offset = shift;
  return placed;
}

// ── The Core-facing half ────────────────────────────────────────────────────────────────────────
//
// Everything above is pure arithmetic and is what the focused test exercises. These are the
// only things that touch the running product, and they exist so that a producer cannot report a
// different number from the one it drew.

// One census line's element identity. `index` is the element's own ordinal within its family (the
// third life orb is index 2), because a family name alone is not an element, and a census that
// cannot say which one it measured is a count rather than a measurement.
struct Element {
  const char *name;
  std::size_t index = 0;
};

// The frame this policy relates. The authored width is the guest's own 512 and the drawn width is
// the number the rest of the render path already asks for, so a third spelling of "how wide is this
// frame" cannot appear here.
Frame frame(Core *core);

// Place an element AND report it, in one call: a producer cannot print a number that differs from
// the one it drew, because there is only one number and it is produced once. A refused element
// comes back unshifted and is reported with its reason.
Placed placeAndReport(const Element &element,
                      Anchor anchor,
                      std::int32_t authoredX,
                      std::int32_t authoredWidth,
                      const Frame &frame);

// The correction for an x that has already been shifted by the framework's centring rule (the HUD
// glyph Mobys, whose x comes out of the widened projection), reported in the same call. It returns
// the number it reports, so a producer cannot draw a different shift from the one the census read.
// A refused element is corrected by zero and reported with its reason.
std::int32_t correctionAndReport(const Element &element, Anchor anchor, const Frame &frame);

// The frame a Core's screen-space UI anchors against: this Core's own `GuestWidescreenOwner` when
// it has one, and otherwise the title's authored window, which is not widening and so corrects by
// zero. `authoredWidth` is the title's own measured native width — the one fact a Core with no
// owner cannot supply about itself. This is here, next to the anchoring policy, so the two titles
// that ship a HUD anchoring override cannot each answer "how wide is this frame" their own way.
[[nodiscard]] Frame widescreenFrame(Core &core, std::uint32_t authoredWidth);

} // namespace spyro::ui_anchor
