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
// honest name: a large-radius actor is now CENSUSED rather than filtered, so `largeRadiusActors`
// must be 1 AND the actor must appear in `visitedWorldActors`.
void test_a_large_render_radius_is_censused_not_dropped() {
  auto game = emptyGame();
  Core &core = game->core;
  constexpr uint32_t kActor = 0x80073000u;
  core.mem_w32(kQueue, kActor);
  // A physically valid actor span, and a mesh index that resolves, so the walk reaches the radius
  // test rather than refusing earlier for an unrelated reason.
  core.mem_w32(kActor + 0x58u, 0x80074000u);
  core.mem_w16(kActor + 0x36u, 0u);
  core.mem_w32(kQueue + 4u, 0u);
  // The whole point: bit 7 of the radius byte SET.
  core.mem_w8(kActor + 0x50u, 0xFFu);
  core.mem_w8(kActor + 0x51u, 0x00u);
  core.mem_w32(kActor + 0x00u, 0u);
  core.mem_w32(kActor + 0x04u, 0u);
  core.mem_w32(kActor + 0x08u, 0u);

  spyro::field_shaded_queue_scene::Frame frame{};
  const auto status = spyro::field_shaded_queue_scene::prepare(&core, 512, frame);
  // Whether the walk READY depends on mesh-table state this fixture does not build; what must not
  // happen is the actor being silently dropped for its radius.
  CHECK(frame.queueRecords >= 1u);
  CHECK_EQ(frame.largeRadiusActors, 1u);
  const bool visited =
      std::find(frame.visitedWorldActors.begin(), frame.visitedWorldActors.end(), kActor) !=
      frame.visitedWorldActors.end();
  // CHECK, not CHECK_MSG: this house has no CHECK_MSG, and the label above states the property.
  CHECK(visited);
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
              frame.largeRadiusActors,
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
  RUN(empty_queue_is_atomic_valid_input);
  RUN(invalid_actor_refuses_without_guest_side_effects);
  inspectSnapshotIfRequested();
  return pt_summary();
}
