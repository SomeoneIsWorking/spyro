#include "field_particle_oriented_submitter.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
using spyro::field_particles_oriented::boxOnScreen;
using spyro::field_particles_oriented::corners;

unsigned checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "field_particle_oriented: %s (check %u)\n", message, checks);
    std::abort();
  }
}

// A square whose corners are all well inside a 512x256 screen. Each case below moves it and says
// which side of the test has to change its answer; a case that only ever passes proves nothing.
std::array<int16_t, 4> xs(int16_t shift) {
  return {(int16_t)(100 + shift),
          (int16_t)(200 + shift),
          (int16_t)(100 + shift),
          (int16_t)(200 + shift)};
}

std::array<int16_t, 4> ys(int16_t shift) {
  return {
      (int16_t)(50 + shift), (int16_t)(50 + shift), (int16_t)(150 + shift), (int16_t)(150 + shift)};
}

void testEachCornerTakesOneOffsetOnEachAxis() {
  const auto placed = corners(30, 40);
  // Rotating by a quarter turn swaps the pair, so `across` and `along` must not both land on the
  // same axis of the same corner: a transposed implementation still draws a square, and still
  // scales with size, but stops turning.
  require(placed[0].first == -30 && placed[0].third == 40, "top-left took the wrong pair");
  require(placed[1].first == 40 && placed[1].third == 30, "top-right took the wrong pair");
  require(placed[2].first == -40 && placed[2].third == -30, "bottom-left took the wrong pair");
  require(placed[3].first == 30 && placed[3].third == -40, "bottom-right took the wrong pair");
}

void testAnUnrotatedSquareIsAxisAligned() {
  // sin 0 is zero and cos 0 is the whole size, so the square degenerates onto one axis pair and
  // opposite corners must mirror exactly.
  const auto placed = corners(0, 50);
  require(placed[0].first == 0 && placed[2].first == -50, "the zero offset moved the wrong corner");
  require(placed[0].third == 50 && placed[3].third == -50, "opposite corners did not mirror");
}

void testAZeroSizeQuadCollapsesOnItsCentre() {
  for (const auto &offset : corners(0, 0)) {
    require(offset.first == 0 && offset.third == 0, "a zero-size quad left its centre");
  }
}

void testAQuadInsideTheScreenIsDrawn() {
  require(boxOnScreen(xs(0), ys(0), 512), "a quad in the middle of the screen was dropped");
}

void testEachEdgeRejectsOnItsOwn() {
  require(!boxOnScreen(xs(0), ys(-200), 512), "a quad entirely above the screen was drawn");
  require(!boxOnScreen(xs(0), ys(300), 512), "a quad entirely below the screen was drawn");
  require(!boxOnScreen(xs(-300), ys(0), 512), "a quad entirely left of the screen was drawn");
  require(!boxOnScreen(xs(450), ys(0), 512), "a quad entirely right of the screen was drawn");
  // One row or column back and each of them is drawn again, so the rejections above are the edge
  // and not some other property of the moved quad.
  require(boxOnScreen(xs(0), ys(105), 512), "the last row above the bottom edge was dropped");
  require(boxOnScreen(xs(311), ys(0), 512), "the last column left of the right edge was dropped");
}

void testAQuadLargerThanTheScreenStillDraws() {
  // No corner is on screen, but the quad covers it. Testing each side independently is the whole
  // reason this case survives; a per-corner "is this one inside" test drops it.
  const std::array<int16_t, 4> wide{-200, 700, -200, 700};
  const std::array<int16_t, 4> tall{-100, -100, 400, 400};
  require(boxOnScreen(wide, tall, 512), "a quad covering the whole screen was dropped");
}

void testTheTopEdgeDependsOnTheColumn() {
  // The guest's threshold is on the packed word, so row 1 at column 0 is above the top edge and the
  // same row one pixel right is below it. Restating the test as `sy > 0` loses exactly this.
  const std::array<int16_t, 4> column0{0, 0, 0, 0};
  const std::array<int16_t, 4> row1{1, 1, 1, 1};
  require(!boxOnScreen(column0, row1, 512), "row 1 at column 0 passed the top edge");
  const std::array<int16_t, 4> column1{1, 1, 1, 1};
  require(boxOnScreen(column1, row1, 512), "row 1 at column 1 failed the top edge");
}

void testTheRightEdgeFollowsTheWidenedWindow() {
  // Everything between 512 and the widened window is off screen for the guest and on screen for the
  // draw. The two answers have to be able to differ, or widescreen would write guest memory.
  const std::array<int16_t, 4> beyond{600, 650, 600, 650};
  require(!boxOnScreen(beyond, ys(0), 512), "a quad past the guest window was called on screen");
  require(boxOnScreen(beyond, ys(0), 720), "a quad inside the widened window was dropped");
}

} // namespace

int main() {
  testEachCornerTakesOneOffsetOnEachAxis();
  testAnUnrotatedSquareIsAxisAligned();
  testAZeroSizeQuadCollapsesOnItsCentre();
  testAQuadInsideTheScreenIsDrawn();
  testEachEdgeRejectsOnItsOwn();
  testAQuadLargerThanTheScreenStillDraws();
  testTheTopEdgeDependsOnTheColumn();
  testTheRightEdgeFollowsTheWidenedWindow();
  std::printf("field_particle_oriented: %u checks passed\n", checks);
  return 0;
}
