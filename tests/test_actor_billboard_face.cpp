#include "actor_billboard_face.h"

#include <cstdio>
#include <cstdlib>

namespace {
using spyro::actor_billboard::extents;
using spyro::actor_billboard::halfHeight;
using spyro::actor_billboard::halfWidth;

unsigned checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "actor_billboard_face: %s (check %u)\n", message, checks);
    std::abort();
  }
}

psxport::native_projection::ProjectionParams projection() {
  return {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341, .dqa = 0, .dqb = 0};
}

psxport::native_projection::NativeProjectedVertex centre(int16_t x, int16_t y, uint16_t z) {
  psxport::native_projection::NativeProjectedVertex vertex{};
  vertex.sx = x;
  vertex.sy = y;
  vertex.sz = z;
  return vertex;
}

// The material packs the two half-extents beside the single colour offset. A test that restated the
// shifts would agree with a transposed implementation, so each case below changes ONE of them and
// says which measurement has to move.
uint32_t material(uint32_t halfW, uint32_t halfH) {
  return (halfW << 10) | (halfH << 1);
}

void testHalfExtentsComeFromTheirOwnFields() {
  require(halfWidth(material(106, 31)) == 106 && halfHeight(material(106, 31)) == 31,
          "the half-extents are not read from their own material fields");
}

void testTheSpriteIsSizedByItsOwnAxis() {
  const auto box = extents(projection(), centre(100, 50, 400), material(106, 31));
  const int32_t width = box.right - box.left;
  const int32_t height = box.bottom - box.top;
  require(width > 0 && height > 0, "a sprite in front of the camera has no area");
  require(width > height, "the wider half-extent did not produce the wider sprite");

  const auto wider = extents(projection(), centre(100, 50, 400), material(212, 31));
  require(wider.right - wider.left > width && wider.bottom - wider.top == height,
          "doubling the horizontal half-extent moved the wrong axis");
  const auto taller = extents(projection(), centre(100, 50, 400), material(106, 62));
  require(taller.bottom - taller.top > height && taller.right - taller.left == width,
          "doubling the vertical half-extent moved the wrong axis");
}

void testTheSpriteShrinksWithDistance() {
  const auto near = extents(projection(), centre(0, 0, 400), material(106, 31));
  const auto far = extents(projection(), centre(0, 0, 1600), material(106, 31));
  require(far.right - far.left < near.right - near.left,
          "the distant sprite is not smaller — the depth cue is not being applied");
  require(far.bottom - far.top < near.bottom - near.top,
          "the distant sprite is not shorter — the depth cue is not being applied");
}

void testTheSpriteStandsOnItsCentre() {
  const auto box = extents(projection(), centre(100, 50, 400), material(106, 31));
  const int32_t width = box.right - box.left;
  const int32_t height = box.bottom - box.top;
  // Retail places the far edge at the centre plus half the extent and derives the near edge by
  // subtraction, so the box is not symmetric about the centre when the extent is odd. Asserting
  // symmetry would have accepted a recentred sprite; assert the rule it actually uses.
  require(box.right == 100 + (width >> 1) && box.left == box.right - width,
          "the horizontal corners are not placed the way the arm places them");
  require(box.bottom == 50 + (height >> 1) && box.top == box.bottom - height,
          "the vertical corners are not placed the way the arm places them");
}

void testAZeroExtentSpriteCollapsesOnTheCentre() {
  const auto box = extents(projection(), centre(7, 9, 400), material(0, 0));
  require(box.left == 7 && box.right == 7 && box.top == 9 && box.bottom == 9,
          "a sprite with no extents did not collapse onto its centre");
}

} // namespace

int main() {
  testHalfExtentsComeFromTheirOwnFields();
  testTheSpriteIsSizedByItsOwnAxis();
  testTheSpriteShrinksWithDistance();
  testTheSpriteStandsOnItsCentre();
  testAZeroExtentSpriteCollapsesOnTheCentre();
  std::printf("actor_billboard_face: %u checks passed\n", checks);
  return 0;
}
