#pragma once

#include <cstdint>
#include <optional>

namespace spyro::wide {

// Spyro's hand-written geometry renderers pack the vertical clip result first and the horizontal
// one second, and the screen limits are two different facts: 256 lines vertically, 512 pixels
// horizontally.
constexpr int32_t kNativeClipHeight = 256;
constexpr int32_t kNativeClipWidth = 512;

// The view-space horizontal plane every hand-written Spyro culler writes as `4*extent < 3*depth`,
// shared by sectors (0x800258F0), the regular and secondary Moby passes (0x8001F158, 0x800208FC)
// and the shaded queue (0x80022A2C). A widened viewport keeps the focal length and grows by
// width/512, so only the depth term scales.
constexpr bool viewHorizontalInside(int32_t extent, int32_t depth, int32_t width) {
  return 4ll * kNativeClipWidth * extent < 3ll * width * depth;
}

// Scaling a negative depth term makes the plane stricter, so OR-ing the native answer in keeps
// the drawn set a superset of the guest's by construction.
constexpr bool drawnHorizontalInside(int32_t extent, int32_t depth, int32_t width) {
  return viewHorizontalInside(extent, depth, kNativeClipWidth) ||
         viewHorizontalInside(extent, depth, width);
}

enum ClipBit : uint32_t {
  kAbove = 1u,
  kBelow = 2u,
  kLeft = 4u,
  kRight = 8u,
};

constexpr uint32_t clipCode(int32_t sx, int32_t sy, int32_t right) {
  uint32_t clip = 0;
  if (sy <= 0) {
    clip |= kAbove;
  }
  if (sy >= kNativeClipHeight) {
    clip |= kBelow;
  }
  if (sx <= 0) {
    clip |= kLeft;
  }
  if (sx >= right) {
    clip |= kRight;
  }
  return clip;
}

// Actor renderers store SXY with the clip code in its low five bits, the same axis policy as
// clipCode() in the guest renderer's packed scratch-word format.
constexpr uint32_t packedClipStatus(uint32_t sxy, int32_t right) {
  const int32_t sx = (int16_t)sxy;
  const int32_t sy = (int16_t)(sxy >> 16);
  return (sxy << 5) | clipCode(sx, sy, right);
}

// `lui rX,0x0200` materialises 512<<16, the horizontal right edge in the packed screen word.
// `lui rX,0x0100` is the separate 256<<16 vertical edge and must never pass this predicate.
constexpr bool isRightBoundLoad(uint32_t instruction) {
  return (instruction & 0xFFE0FFFFu) == 0x3C000200u;
}

constexpr std::optional<uint32_t> replaceRightBound(uint32_t instruction, int32_t right) {
  if (!isRightBoundLoad(instruction)) {
    return std::nullopt;
  }
  return (instruction & 0xFFFF0000u) | (uint32_t)(right & 0xFFFF);
}

} // namespace spyro::wide
