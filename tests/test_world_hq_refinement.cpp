#include "world_hq_refinement.h"
#include "world_projection_math.h"

#include <cstdlib>

namespace {

using psxport::native_projection::FixedAffine;
using psxport::native_projection::ProjectionParams;
using spyro::world_hq_refinement::HighVertex;
using spyro::world_hq_refinement::Position;

void require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

FixedAffine identity() {
  FixedAffine out{};
  out.m[0][0] = out.m[1][1] = out.m[2][2] = 4096;
  return out;
}

HighVertex project(const ProjectionParams &projection, Position position, uint8_t tags) {
  return spyro::world_hq_refinement::projectVertex(identity(), projection, position, tags, 512);
}

void test_depth_and_clip_paths() {
  const ProjectionParams ordinary{0, 100 << 16, 256};

  const HighVertex raw = project(ordinary, {0, 0, 512}, 0);
  require(raw.projected.sz == 512);
  require(raw.projected.viewZ == 512.0f);
  require(raw.projected.clip == 0);

  // The coarse encoder treats x==0 as inside. The precision encoder retains
  // the guest's directional x<=0 bit instead.
  const HighVertex coarse = project(ordinary, {0, 0, 512}, 1);
  const HighVertex precision = project(ordinary, {0, 0, 512}, 2);
  require(coarse.projected.clip == 0);
  require(precision.projected.clip == 4);

  // Tags 2/3 return to the coarse encoder at the original SZ 0x600 boundary.
  const HighVertex far = project({0, 100 << 16, 4000}, {0, 0, 0x600}, 2);
  require(far.projected.sz == 0x600);
  require(far.projected.clip == 0);
}

void test_projection_flag_facing_gate() {
  // H > 2*SZ sets the RTPS divide-overflow/checksum flag. Only tag-2/3
  // precision vertices encode that result into the queued parent's NCLIP
  // recheck bit.
  const ProjectionParams overflow{0, 100 << 16, 700};
  const HighVertex precision = project(overflow, {0, 0, 300}, 2);
  require(precision.requiresFacingCheck);

  const HighVertex coarseTag = project(overflow, {0, 0, 300}, 1);
  require(!coarseTag.requiresFacingCheck);

  const HighVertex coarseDepth = project({0, 100 << 16, 4000}, {0, 0, 0x600}, 2);
  require(!coarseDepth.requiresFacingCheck);

  // Opposite-answer control: removing the overflow produces the same source
  // position but must clear the semantic gate.
  const HighVertex noOverflow = project({0, 100 << 16, 256}, {0, 0, 300}, 2);
  require(!noOverflow.requiresFacingCheck);
}

void test_packed_projection_input_borrow() {
  const auto borrowed = spyro::world_projection_math::packProjectionInput(-5457, -1541, 1234);
  require(borrowed.x == -5457);
  require(borrowed.y == -1542);
  require(borrowed.z == 1234);

  // Opposite-answer control: a nonnegative low half does not modify the
  // authored high half.
  const auto noBorrow = spyro::world_projection_math::packProjectionInput(5457, -1541, 1234);
  require(noBorrow.x == 5457);
  require(noBorrow.y == -1541);
  require(noBorrow.z == 1234);
}

void test_near_quad_color_graph() {
  const auto colors = spyro::world_hq_refinement::nearQuadColorLattice(
      {0x00000000u, 0x00202020u, 0x00404040u, 0x00606060u});
  require(colors[0] == 0x00000000u && colors[4] == 0x00202020u);
  require(colors[20] == 0x00606060u && colors[24] == 0x00404040u);
  require(colors[12] == 0x00404040u);
  require(colors[2] == 0x00101010u && colors[7] == 0x00282828u);
  require(colors[10] == 0x00303030u && colors[11] == 0x00383838u);
  require(colors[14] == 0x00303030u && colors[13] == 0x00383838u);
  require(colors[22] == 0x00505050u && colors[17] == 0x00484848u);
  require(colors[6] == 0x00202020u && colors[8] == 0x00303030u);
  require(colors[16] == 0x00505050u && colors[18] == 0x00404040u);

  // This is the falsifier for the old recursive geometry graph: it produced
  // 0x1c at lattice[6] for these corners instead of the guest's direct 0x20.
  require(colors[6] != 0x001c1c1cu);
}

} // namespace

int main() {
  test_depth_and_clip_paths();
  test_projection_flag_facing_gate();
  test_packed_projection_input_borrow();
  test_near_quad_color_graph();
  return 0;
}
