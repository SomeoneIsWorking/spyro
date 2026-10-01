// spyro2_widescreen_math.h — the pure horizontal arithmetic of Spyro 2's widening, with no Core.
//
// Every number this file produces is a function of the GUEST'S OWN published projection and the
// resolved plan, so each one can be pinned by a unit test without a disc, a core, or a frame.
// The owner in spyro2_widescreen.h is the only thing that reads a Core or the GTE, and it does
// nothing but decide WHEN these are applied.
#pragma once

#include <cstdint>

namespace spyro2::widescreen_math {

// Spyro 2's authored horizontal window, MEASURED: the display bootstrap's own geometry-init leaf
//
//   80011D24  addiu $sp,$sp,-0x18
//   80011D34  addiu $a0,$zero,0x100      ; 256
//   80011D38  jal   0x80057AF8           ; SetGeomOffset(256, ...)
//   80011D3C  addiu $a1,$zero,0x78       ; 120
//   80011D40  jal   0x80057AE8           ; SetGeomScreen(0x155)
//   80011D44  addiu $a0,$zero,0x155      ; 341
//
// states OFX = 256 for a 512-dot display mode (`gp1_display_width` decodes the NTSC 512 mode),
// and the guest's own view is therefore 512 wide with its centre at half that. Both the widened
// centre and the widened clip edge below are stated as multiples of that authored width rather
// than as literals, so a change of authored width moves them together.
inline constexpr int kGuestOfx = 256;
inline constexpr int kNativeWidth = 2 * kGuestOfx;
inline constexpr int kNativeHeight = 240;

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
// the guest's 512 columns land on 86..597, and the measured capture shows ink on 0..511 and nothing
// beyond — which is the projection shifted and the extra geometry clipped, two separate facts that
// read the same in one picture. Keeping the arithmetic here lets a test state the expectation
// instead of a comment restating it after the fact.
constexpr int guestWindowLeft(int projectionWidth) {
  return projectionWidth / 2 - kGuestOfx;
}

constexpr int guestWindowRight(int projectionWidth) {
  return projectionWidth / 2 + kGuestOfx - 1;
}

// The inclusive right edge the GPU's clipping rectangle is widened to. Retail's own edge is the
// last column of the authored window; a wider picture has to be allowed to be written out to the
// last column of the wider one, or the extra geometry is rasterised and then thrown away.
//
// Never narrows: a plan that is not a widening leaves the guest's own edge exactly as it was.
constexpr int widenedDrawAreaRight(int guestAreaRight, int presentationWidth) {
  if (presentationWidth <= kNativeWidth) {
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

} // namespace spyro2::widescreen_math
