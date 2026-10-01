// spyro2_terrain_screen.h — the horizontal screen tests of Spyro 2's terrain drawer (SCUS_944.25
// 0x80023BB4), with no Core and no GTE.
//
// WHAT RETAIL TESTS. Every projected vertex the drawer keeps is classified against the authored
// 512-dot window, and a polygon whose vertices are all outside one edge is not drawn. The image
// spells the horizontal half of that test three ways, each on the packed SXY word the GTE returns
// (x in the low halfword, y in the high one):
//
//   outside either edge   (sxy & 0xFE00) != 0             x < 0 || x >= 512, as one mask
//   at or left of left    (int32)(sxy << 16) <= 0         x <= 0
//   at or right of right  (int32)((sxy << 16) - 0x2000000) >= 0    x >= 512
//
// (the vertical tests compare the whole word against 13 << 16 and 228 << 16 and are not touched).
//
// WIDESCREEN. At 16:9 the presentation shows the guest's picture with `margin` extra columns on
// each side, so the window a vertex must leave before its polygon may be dropped is [-margin, 512 +
// margin). The three tests are restated against that window with the same integer shapes: the
// shifted comparisons keep retail's wrap-around arithmetic, and the mask becomes the signed range
// test it is equivalent to. With `margin == 0` each test computes exactly what retail computes, so
// 4:3 is bit-for-bit the image.
#pragma once

#include <cstdint>

namespace spyro2::terrain {

class ScreenBounds {
public:
  // Retail's authored window.
  static constexpr std::int32_t kNativeWidth = 512;

  constexpr ScreenBounds() = default;
  // A window `margin` columns wider on each side. Never narrower than retail's.
  explicit constexpr ScreenBounds(std::int32_t margin)
      : left_(margin > 0 ? -margin : 0), right_(kNativeWidth + (margin > 0 ? margin : 0)) {}

  [[nodiscard]] constexpr std::int32_t left() const {
    return left_;
  }
  [[nodiscard]] constexpr std::int32_t right() const {
    return right_;
  }

  // `(sxy & 0xFE00) != 0`: x < left || x >= right.
  [[nodiscard]] constexpr bool outsideHorizontal(std::uint32_t sxy) const {
    const std::int32_t x = static_cast<std::int16_t>(sxy & 0xFFFFu);
    return x < left_ || x >= right_;
  }

  // `(int32)(sxy << 16) <= 0`: x <= left.
  [[nodiscard]] constexpr bool atOrLeftOfLeft(std::uint32_t sxy) const {
    return static_cast<std::int32_t>((sxy << 16) - (static_cast<std::uint32_t>(left_) << 16)) <= 0;
  }

  // `(int32)((sxy << 16) - 0x2000000) >= 0`: x >= right.
  [[nodiscard]] constexpr bool atOrRightOfRight(std::uint32_t sxy) const {
    return static_cast<std::int32_t>((sxy << 16) - (static_cast<std::uint32_t>(right_) << 16)) >= 0;
  }

private:
  std::int32_t left_ = 0;
  std::int32_t right_ = kNativeWidth;
};

// The vertical tests, which widescreen leaves as retail's.
inline constexpr std::uint32_t kScreenTop = 0x000D0000u;    // sxy - kScreenTop <= 0: above
inline constexpr std::uint32_t kScreenBottom = 0x00E40000u; // sxy - kScreenBottom >= 0: below

[[nodiscard]] constexpr bool aboveTop(std::uint32_t sxy) {
  return static_cast<std::int32_t>(sxy - kScreenTop) <= 0;
}

[[nodiscard]] constexpr bool belowBottom(std::uint32_t sxy) {
  return static_cast<std::int32_t>(sxy - kScreenBottom) >= 0;
}

} // namespace spyro2::terrain
