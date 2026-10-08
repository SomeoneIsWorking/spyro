#include "wide_clip_plan.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

} // namespace

int main() {
  using namespace spyro::wide;

  expect(isRightBoundLoad(0x3C0F0200u), "512 horizontal bound is accepted");
  expect(!isRightBoundLoad(0x3C0E0100u), "256 vertical bound is rejected");
  expect(replaceRightBound(0x3C0F0200u, 896) == 0x3C0F0380u,
         "widening preserves the destination register");
  expect(!replaceRightBound(0x3C0E0100u, 896), "vertical bound cannot be replaced");

  expect(clipCode(100, -1, 896) == kAbove, "top is a vertical clip");
  expect(clipCode(100, 256, 896) == kBelow, "bottom stays at 256 lines");
  expect(clipCode(-1, 100, 896) == kLeft, "left is a horizontal clip");
  expect(clipCode(896, 100, 896) == kRight, "right follows the wide width");
  expect(clipCode(700, 100, kNativeClipWidth) == kRight,
         "the same vertex is outside at native width");
  expect(clipCode(700, 100, 896) == 0u, "the same vertex is recovered by widescreen");
  expect((packedClipStatus(700u | (100u << 16), 896) & 31u) == 0u,
         "packed actor clip agrees with the wide policy");
  expect((packedClipStatus(896u | (100u << 16), 896) & kRight) != 0u,
         "packed actor clip recognizes the wide right edge");
  expect(clipCode(100, 300, kNativeClipWidth) == clipCode(100, 300, 896),
         "widescreen never changes vertical clipping");

  // The view-space plane: at native width it is exactly retail's `4*e - 3*d < 0`, strictly.
  expect(viewHorizontalInside(299, 400, kNativeClipWidth), "4*299 < 3*400 is inside");
  expect(!viewHorizontalInside(300, 400, kNativeClipWidth), "4*300 == 3*400 is outside (strict)");
  // The plane is retail's `4*e - 3*d < 0` and nothing else, so it admits a NEGATIVE pair: -2048 <
  // -1536. What keeps a behind-the-eye sector out is the depth term every caller applies beside it
  // (`z + radius > 0` in the sector cull, `z - radius < 0 && z + extent > 0` in the Moby planes),
  // never this one. Asserting the opposite here would be asserting a bound retail does not have.
  expect(viewHorizontalInside(-1, -1, kNativeClipWidth),
         "the plane alone admits a negative pair; the depth term is the near bound");
  // A widened width admits the margin only; it never removes what the native plane admitted.
  expect(viewHorizontalInside(300, 400, 684), "the native edge is inside the 684 plane");
  expect(!viewHorizontalInside(401, 400, 684), "4*512*401 >= 3*684*400 stays outside at 684");
  expect(viewHorizontalInside(400, 400, 684), "4*512*400 < 3*684*400 is inside at 684");
  // Scaling a NEGATIVE depth term makes it MORE negative, so widening TIGHTENS the plane: the raw
  // wide plane loses this acceptance that the native one had. Native admits |extent| >
  // 0.75*|depth|; 684 admits only |extent| > 1.00195*|depth|. 1025/1024 is inside native and
  // outside 684 — which is why the drawn answer ORs the native one instead of scaling alone.
  expect(viewHorizontalInside(-1025, -1024, kNativeClipWidth) &&
             !viewHorizontalInside(-1025, -1024, 684),
         "a raw wide plane is stricter than native behind the depth origin");
  expect(drawnHorizontalInside(-1025, -1024, 684), "the drawn answer keeps the native acceptance");
  uint32_t scanned = 0, nativeInside = 0, marginOnly = 0;
  for (int32_t extent = -600; extent <= 600; extent += 7) {
    for (int32_t depth = -50; depth <= 800; depth += 13) {
      ++scanned;
      const bool native = viewHorizontalInside(extent, depth, kNativeClipWidth);
      const bool drawn = drawnHorizontalInside(extent, depth, 684);
      nativeInside += native ? 1u : 0u;
      marginOnly += drawn && !native ? 1u : 0u;
      if (native && !drawn) {
        expect(false, "the drawn answer must be a superset of the native one");
      }
      if (drawnHorizontalInside(extent, depth, kNativeClipWidth) != native) {
        expect(false, "at native width the drawn answer IS the native one");
      }
    }
  }
  std::printf("plane sweep: scanned %u, native inside %u, margin only %u\n",
              scanned,
              nativeInside,
              marginOnly);
  expect(nativeInside > 0 && marginOnly > 0, "the sweep must reach both answers");

  if (failures != 0) {
    return 1;
  }
  std::puts("wide clip plan: PASS");
  return 0;
}
