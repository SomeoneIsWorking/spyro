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
  expect(clipCode(100, 300, kNativeClipWidth) == clipCode(100, 300, 896),
         "widescreen never changes vertical clipping");

  if (failures != 0) {
    return 1;
  }
  std::puts("wide clip plan: PASS");
  return 0;
}
