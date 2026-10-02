// guest_widescreen_math.h — the pure horizontal arithmetic of the guest-projection widening, with
// no Core and no GTE.
//
// Every number this file produces is a function of the GUEST'S OWN published projection and the
// resolved plan, so each one can be pinned by a unit test without a disc, a core, or a frame. The
// owner in guest_widescreen_owner.h is the only thing that reads a Core or the GTE, and it does
// nothing but decide WHEN these are applied.
//
// Nothing here is a TITLE's number. The authored horizontal centre each title states through its
// own geometry init is a fact of that title and arrives as `guestOffsetX`; everything below is
// stated as a multiple of that authored width, so a change of authored width moves the whole
// policy with it and two titles of this engine family share one implementation.
#pragma once

#include <cstdint>

namespace spyro::guest_widescreen_math {

// The horizontal centre a WIDER window puts its origin at. This is the whole of the projection
// widening: the vertical centre and the projection distance are untouched, so the vertical field
// of view is retail's and only the horizontal one grows.
//
// It is deliberately a pure function of the plan rather than a constant: `GuestProjectionPlan`
// already resolved `projectionExtent.width` from the requested aspect, and re-deriving the factor
// here would be a second implementation of the same ratio.
constexpr int widenedCenterX(int projectionWidth) {
  return projectionWidth / 2;
}

// The half-open column range the GUEST's OWN picture occupies once the centre moves, which is the
// range a capture is expected to show and the range its complement is not.
//
// This exists because "the picture is `centre +/- half the authored width`" is the claim every one
// of these numbers rests on, and it is exactly what the capture measures: with the centre at 342
// and a 512-wide guest view, the guest's columns land on 86..597, and a capture shows ink inside
// that range and a margin outside it -- which are two separate facts that read the same in one
// picture. Keeping the arithmetic here lets a test state the expectation instead of a comment
// restating it after the fact.
constexpr int guestWindowLeft(int projectionWidth, int guestOffsetX) {
  return projectionWidth / 2 - guestOffsetX;
}

constexpr int guestWindowRight(int projectionWidth, int guestOffsetX) {
  return projectionWidth / 2 + guestOffsetX - 1;
}

// The inclusive right edge the GPU's clipping rectangle is widened to. Retail's own edge is the
// last column of the authored window; a wider picture has to be allowed to be written out to the
// last column of the wider one, or the extra geometry is rasterised and then thrown away.
//
// Never narrows, and never widens at all unless the plan really is wider than the authored
// window: a 4:3 plan leaves the guest's own edge exactly as it was, which is what makes the 4:3
// register state identical rather than merely equivalent.
constexpr int widenedDrawAreaRight(int guestAreaRight, int presentationWidth, int nativeWidth) {
  if (presentationWidth <= nativeWidth) {
    return guestAreaRight;
  }
  const int widened = presentationWidth - 1;
  return guestAreaRight > widened ? guestAreaRight : widened;
}

// The two words one re-issued GP1 draw-area corner is built from, as the hardware packs them:
// X in bits 0..9 and Y in bits 10..18 for BOTH corners, with the command byte in bits 24..31.
//
// Written out rather than pushed through a helper because the framework's own decoder
// (`Gp0Command::drawAreaCorner`) reads exactly this packing, and a second spelling of it would be
// a second answer to the same question.
constexpr std::uint32_t drawAreaTopLeft(int x, int y) {
  return 0xE3000000u | (static_cast<std::uint32_t>(y & 0x1FF) << 10) |
         static_cast<std::uint32_t>(x & 0x3FF);
}

constexpr std::uint32_t drawAreaBottomRight(int x, int y) {
  return 0xE4000000u | (static_cast<std::uint32_t>(y & 0x1FF) << 10) |
         static_cast<std::uint32_t>(x & 0x3FF);
}

} // namespace spyro::guest_widescreen_math
