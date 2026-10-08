// The pure horizontal arithmetic of the guest-projection widening: no Core, no GTE, and every
// number a function of the guest's own published projection and the resolved plan. Only the owner
// in guest_widescreen_owner.h reads a Core or the GTE, and it decides WHEN these are applied.
//
// Each title states its own authored horizontal centre as `guestOffsetX`; everything below is a
// multiple of that authored width, so one implementation serves the whole engine family.
#pragma once

#include <cstdint>

namespace spyro::guest_widescreen_math {

// The half-open column range the GUEST's OWN picture occupies once the centre moves: ink inside
// this range and a margin outside it are two separate facts a capture shows separately.
constexpr int guestWindowLeft(int projectionWidth, int guestOffsetX) {
  return projectionWidth / 2 - guestOffsetX;
}

constexpr int guestWindowRight(int projectionWidth, int guestOffsetX) {
  return projectionWidth / 2 + guestOffsetX - 1;
}

// The inclusive right edge the clipping rectangle is widened to. Never narrows, and never widens
// unless the plan really is wider than the authored window, so a 4:3 plan leaves the guest's own
// edge exactly as it was.
constexpr int widenedDrawAreaRight(int guestAreaRight, int presentationWidth, int nativeWidth) {
  if (presentationWidth <= nativeWidth) {
    return guestAreaRight;
  }
  const int widened = presentationWidth - 1;
  return guestAreaRight > widened ? guestAreaRight : widened;
}

// The two words one re-issued GP1 draw-area corner is built from: X in bits 0..9 and Y in bits
// 10..18 for BOTH corners, command byte in bits 24..31, as the hardware packs them.
constexpr std::uint32_t drawAreaTopLeft(int x, int y) {
  return 0xE3000000u | (static_cast<std::uint32_t>(y & 0x1FF) << 10) |
         static_cast<std::uint32_t>(x & 0x3FF);
}

constexpr std::uint32_t drawAreaBottomRight(int x, int y) {
  return 0xE4000000u | (static_cast<std::uint32_t>(y & 0x1FF) << 10) |
         static_cast<std::uint32_t>(x & 0x3FF);
}

} // namespace spyro::guest_widescreen_math
