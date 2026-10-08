#include "core.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_scene.h"
#include "game.h"
#include "testutil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>

namespace {

constexpr uint32_t kQueue = 0x800720f4u;
constexpr uint32_t kShadowCursor = 0x80075f00u;

// The scene reads the host aspect policy through the GPU owner, so the fixture owns a Game the way
// the shipping callers do; a bare Core has no render mode to ask.
std::unique_ptr<Game> emptyGame() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  core.rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core.rsub.projParams.setGeomScreen(341.0f);
  core.mem_w32(kQueue, 0u);
  core.mem_w32(kShadowCursor, 0x80072500u);
  game->mods.aspect = ASPECT_4_3;
  return game;
}

constexpr uint32_t kMeshTable = 0x80076378u;
constexpr uint32_t kMesh = 0x80074000u;

// A one-primitive, three-vertex mesh behind model index 0: header, its vertex words, the
// primitive stream (indices word, normal word) and the per-vertex normals that follow the header.
void buildMesh(Core &core) {
  core.mem_w32(kMeshTable, kMesh);
  core.mem_w8(kMesh + 0u, 3u);
  core.mem_w8(kMesh + 1u, 1u);
  core.mem_w32(kMesh + 4u, 0x80074200u);
  core.mem_w32(kMesh + 12u, 0x80074300u);
  core.mem_w32(0x80074300u, (1u << 23) | (2u << 16) | (2u << 9) | (2u << 2) | 1u);
  core.mem_w32(0x80074304u, 0x00010000u);
}

void placeActor(Core &core, uint32_t actor, uint8_t radius, int32_t x, int32_t y, int32_t z) {
  core.mem_w32(kQueue, actor);
  core.mem_w32(kQueue + 4u, 0u);
  core.mem_w32(actor + 0x58u, 0x80074000u);
  core.mem_w16(actor + 0x36u, 0u);
  core.mem_w8(actor + 0x50u, radius);
  core.mem_w32(actor + 12u, (uint32_t)x);
  core.mem_w32(actor + 16u, (uint32_t)y);
  core.mem_w32(actor + 20u, (uint32_t)z);
}

// A moby whose RENDER RADIUS has bit 7 set must be WALKED, not dropped.
//
// This is the regression test for the removed `mem_r8(actor + 0x50) & 0x80` skip. Byte 0x50 is the
// guest's `m_RenderRadius` -- a `u_char` clipping radius (external/spyro-1/include/moby.h:133) --
// and `hud_text_builder.cpp` writes 0xFF there for every glyph, because a glyph wants the largest
// radius. Under the old test every HUD glyph was therefore dropped, which is why the pause menu
// drew a panel and a border and NO CAPTIONS, and why the level-transition tally's captions were
// missing too.
//
// The case is built so it FAILS if the skip comes back, and so it also pins the counter's new
// honest name: a large-radius actor is now CENSUSED rather than filtered, so `screenSpaceActors`
// must be 1 AND the actor must appear in `visitedWorldActors`.
void test_a_large_render_radius_is_censused_not_dropped() {
  auto game = emptyGame();
  Core &core = game->core;
  constexpr uint32_t kActor = 0x80073000u;
  // A physically valid actor and a mesh that resolves, so the walk reaches the radius test and a
  // refusal -- which wipes the census -- cannot be mistaken for the actor being dropped.
  buildMesh(core);
  placeActor(core, kActor, 0xFFu, 0, 0, 0);

  spyro::field_shaded_queue_scene::Frame frame{};
  const auto status = spyro::field_shaded_queue_scene::prepare(&core, 512, frame);
  CHECK(status == spyro::field_shaded_queue_scene::Status::Ready);
  CHECK(frame.queueRecords >= 1u);
  CHECK_EQ(frame.screenSpaceActors, 1u);
  const bool visited =
      std::find(frame.visitedWorldActors.begin(), frame.visitedWorldActors.end(), kActor) !=
      frame.visitedWorldActors.end();
  // CHECK, not CHECK_MSG: this house has no CHECK_MSG, and the label above states the property.
  CHECK(visited);
}

void test_a_screen_space_moby_is_projected_about_its_own_centre() {
  auto game = emptyGame();
  Core &core = game->core;
  buildMesh(core);
  constexpr uint32_t kActor = 0x80073000u;
  placeActor(core, kActor, 0xFFu, 90, 2, 1440);
  spyro::field_shaded_queue_scene::Frame frame{};
  const auto status = spyro::field_shaded_queue_scene::prepare(&core, 512, frame);
  CHECK(status == spyro::field_shaded_queue_scene::Status::Ready);
  CHECK_EQ(frame.screenSpaceActors, 1u);
  CHECK_EQ(frame.input.records.size(), 1u);
  if (frame.input.records.size() == 1u) {
    const auto &record = frame.input.records[0];
    CHECK(record.projection.has_value());
    if (record.projection) {
      // 4:3: no widening offset, so the centre is the guest's own.
      CHECK_EQ(record.projection->ofx, 90 << 16);
      CHECK_EQ(record.projection->ofy, 2 << 16);
    }
    CHECK_EQ(record.affine.t[2], 720);
    CHECK_EQ(record.affine.m[1][1], 0xA00);
    CHECK_EQ(record.affine.m[0][0], 0x1000);
  }
}

void test_a_world_moby_keeps_the_frame_projection() {
  auto game = emptyGame();
  Core &core = game->core;
  buildMesh(core);
  constexpr uint32_t kActor = 0x80073000u;
  placeActor(core, kActor, 0x10u, 0, 0, 0);
  spyro::field_shaded_queue_scene::Frame frame{};
  spyro::field_shaded_queue_scene::prepare(&core, 512, frame);
  CHECK_EQ(frame.screenSpaceActors, 0u);
  CHECK_EQ(frame.input.records.size(), 1u);
  for (const auto &record : frame.input.records) {
    CHECK(!record.projection.has_value());
  }
}

void test_empty_queue_is_atomic_valid_input() {
  auto game = emptyGame();
  Core &core = game->core;
  spyro::field_shaded_queue_scene::Frame frame{};
  CHECK(spyro::field_shaded_queue_scene::prepare(&core, 512, frame) ==
        spyro::field_shaded_queue_scene::Status::Ready);
  const auto recipe = spyro::field_shaded_queue_recipe::derive(frame.input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::ValidEmpty);
  CHECK_EQ(frame.queueRecords, 0u);
  CHECK_EQ(frame.input.records.size(), 0u);
}

void test_invalid_actor_refuses_without_guest_side_effects() {
  auto game = emptyGame();
  Core &core = game->core;
  core.mem_w32(kQueue, 0x807ffff0u);
  core.mem_w32(kQueue + 4u, 0u);
  spyro::field_shaded_queue_scene::Frame frame{};
  CHECK(spyro::field_shaded_queue_scene::prepare(&core, 512, frame) ==
        spyro::field_shaded_queue_scene::Status::InvalidActor);
  CHECK_EQ(core.mem_r32(kShadowCursor), 0x80072500u);
}

void inspectSnapshotIfRequested() {
  const char *path = std::getenv("PSXPORT_FIELD_SHADED_SNAPSHOT");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  auto game = emptyGame();
  Core &core = game->core;
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  input.read(reinterpret_cast<char *>(core.ram), sizeof(core.ram));
  CHECK(input.gcount() == static_cast<std::streamsize>(sizeof(core.ram)));
  core.rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core.rsub.projParams.setGeomScreen(341.0f);

  spyro::field_shaded_queue_scene::Frame frame{};
  const auto scene = spyro::field_shaded_queue_scene::prepare(&core, 512, frame);
  const auto recipe = spyro::field_shaded_queue_recipe::derive(frame.input);
  const std::set<uint16_t> meshes(frame.sourceMeshIndices.begin(), frame.sourceMeshIndices.end());
  const std::set<int32_t> lightingOffsets(frame.sourceLightingOffsets.begin(),
                                          frame.sourceLightingOffsets.end());
  std::printf("field shaded snapshot: scene=%s queue=%u large_radius=%u valid_mesh=%u null=%u "
              "source_candidates=%u visible=%zu culled=%u visible_candidates=%u recipe=%u "
              "recipe_candidates=%u rejected=%u faces=%zu meshes=%zu lighting=%zu shadows=%zu\n",
              spyro::field_shaded_queue_scene::statusName(scene),
              frame.queueRecords,
              frame.screenSpaceActors,
              frame.validMeshRecords,
              frame.nullMeshes,
              frame.validMeshPrimitiveCandidates,
              frame.input.records.size(),
              frame.culled,
              frame.primitiveCandidates,
              (uint32_t)recipe.status,
              recipe.candidates,
              recipe.rejected,
              recipe.faces.size(),
              meshes.size(),
              lightingOffsets.size(),
              frame.shadows.size());
  CHECK(scene == spyro::field_shaded_queue_scene::Status::Ready);
  CHECK_EQ(frame.queueRecords, 93u);
  CHECK_EQ(frame.validMeshRecords, 52u);
  CHECK_EQ(frame.nullMeshes, 41u);
  CHECK_EQ(frame.validMeshPrimitiveCandidates, 936u);
  CHECK_EQ(frame.input.records.size(), 3u);
  CHECK_EQ(frame.primitiveCandidates, 54u);
  CHECK_EQ(meshes.size(), 2u);
  CHECK(meshes.contains(83u));
  CHECK(meshes.contains(84u));
  CHECK_EQ(lightingOffsets.size(), 2u);
  CHECK(lightingOffsets.contains(8));
  CHECK(lightingOffsets.contains(16));
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.candidates, 54u);
}

} // namespace

int main() {
  RUN(a_large_render_radius_is_censused_not_dropped);
  RUN(a_screen_space_moby_is_projected_about_its_own_centre);
  RUN(a_world_moby_keeps_the_frame_projection);
  RUN(empty_queue_is_atomic_valid_input);
  RUN(invalid_actor_refuses_without_guest_side_effects);
  inspectSnapshotIfRequested();
  return pt_summary();
}
