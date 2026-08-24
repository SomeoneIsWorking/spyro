#include "world_chunk_codec.h"
#include "world_hq_refinement.h"
#include "world_projection_math.h"

#include <cstdlib>
#include <vector>

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

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) {
    ram[address + i] = (uint8_t)(value >> (i * 8u));
  }
}

void writeIdentity(std::vector<uint8_t> &ram, uint32_t address) {
  w32(ram, address, 0x00001000u);
  w32(ram, address + 4u, 0u);
  w32(ram, address + 8u, 0x00001000u);
  w32(ram, address + 12u, 0u);
  w32(ram, address + 16u, 0x00001000u);
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

void test_near_quad_texture_attribute() {
  constexpr uint32_t kCamera = 0x76dd0u;
  constexpr uint32_t kEnvironment = 0x785a8u;
  constexpr uint32_t kMaterial = 0x90000u;
  std::vector<uint8_t> bytes(0x200000u);
  writeIdentity(bytes, kCamera);
  w32(bytes, kEnvironment + 0x1cu, kMaterial);
  w32(bytes, kEnvironment + 0x20u, 1u);

  // The executable's attribute-0x68 table is the FIELD case that was absent
  // from the original hermetic corpus. It rotates the authored e0/ff U pair
  // by +31/-31 and supplies the matching lower two corners.
  w32(bytes, 0x6d0c0u, 0xffe1001fu);
  w32(bytes, 0x6d0c4u, 0x1f001f1fu);
  for (uint32_t child = 0; child < 16u; ++child) {
    const uint32_t pair = kMaterial + 0x28u + child * 8u;
    w32(bytes, pair, 0x2420e0e0u);
    w32(bytes, pair + 4u, 0xd088e0ffu);
  }

  spyro::world_hq_refinement::Work work{};
  spyro::world_hq_refinement::Parent parent{};
  parent.count = 4;
  parent.materialWord = 0;
  parent.statusAddress = 0x100u;
  parent.vertices[0].position = {-128, -128, 1024};
  parent.vertices[1].position = {128, -128, 1024};
  parent.vertices[2].position = {128, 128, 1024};
  parent.vertices[3].position = {-128, 128, 1024};
  for (auto &vertex : parent.vertices) {
    vertex.projected.rgb = 0x00406080u;
  }
  work.near.push_back(parent);

  spyro::world_recipe::Recipe recipe{};
  const char *why = "none";
  const ProjectionParams projection{256 << 16, 120 << 16, 341};
  require(spyro::world_hq_refinement::append(
      spyro::world_chunk_codec::RamView(bytes), projection, 512, work, recipe, why));
  require(recipe.faces.size() == 16u);
  for (uint32_t child = 0; child < recipe.faces.size(); ++child) {
    const auto &face = recipe.faces[child];
    require(face.vertexCount == 4u && face.material.clut == 0x2420u &&
            face.material.tpage == 0xd088u);
    require(face.textureSource == kMaterial + 0x28u + child * 8u);
    require(face.vertices[0].u == 0xffu && face.vertices[1].u == 0xe0u &&
            face.vertices[2].u == 0xffu && face.vertices[3].u == 0xe0u);
  }
}

void test_medium_quad_texture_attribute() {
  constexpr uint32_t kCamera = 0x76dd0u;
  constexpr uint32_t kEnvironment = 0x785a8u;
  constexpr uint32_t kMaterial = 0x90000u;
  std::vector<uint8_t> bytes(0x200000u);
  writeIdentity(bytes, kCamera);
  w32(bytes, kEnvironment + 0x1cu, kMaterial);
  w32(bytes, kEnvironment + 0x20u, 1u);

  w32(bytes, 0x6d0c0u, 0xffe1001fu);
  w32(bytes, 0x6d0c4u, 0x1f001f1fu);
  for (uint32_t child = 0; child < 4u; ++child) {
    const uint32_t pair = kMaterial + 8u + child * 8u;
    w32(bytes, pair, 0x2420e0e0u);
    w32(bytes, pair + 4u, 0xd088e0ffu);
  }

  spyro::world_hq_refinement::Work work{};
  spyro::world_hq_refinement::Parent parent{};
  parent.count = 4;
  parent.materialWord = 0;
  parent.statusAddress = 0x100u;
  parent.vertices[0].position = {-128, -128, 1024};
  parent.vertices[1].position = {128, -128, 1024};
  parent.vertices[2].position = {128, 128, 1024};
  parent.vertices[3].position = {-128, 128, 1024};
  for (auto &vertex : parent.vertices) {
    vertex.projected.rgb = 0x00406080u;
  }
  work.medium.push_back(parent);

  spyro::world_recipe::Recipe recipe{};
  const char *why = "none";
  const ProjectionParams projection{256 << 16, 120 << 16, 341};
  require(spyro::world_hq_refinement::append(
      spyro::world_chunk_codec::RamView(bytes), projection, 512, work, recipe, why));
  require(recipe.faces.size() == 4u);
  for (uint32_t child = 0; child < recipe.faces.size(); ++child) {
    const auto &face = recipe.faces[child];
    require(face.origin == spyro::world_recipe::Origin::Medium && face.vertexCount == 4u &&
            face.material.clut == 0x2420u && face.material.tpage == 0xd088u);
    require(face.textureSource == kMaterial + 8u + child * 8u);
    require(face.vertices[0].u == 0xffu && face.vertices[1].u == 0xe0u &&
            face.vertices[2].u == 0xffu && face.vertices[3].u == 0xe0u);
  }
}

} // namespace

int main() {
  test_depth_and_clip_paths();
  test_projection_flag_facing_gate();
  test_packed_projection_input_borrow();
  test_near_quad_color_graph();
  test_near_quad_texture_attribute();
  test_medium_quad_texture_attribute();
  return 0;
}
