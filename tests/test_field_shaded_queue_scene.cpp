#include "core.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_scene.h"
#include "testutil.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>

namespace {

constexpr uint32_t kQueue = 0x800720f4u;
constexpr uint32_t kShadowCursor = 0x80075f00u;

std::unique_ptr<Core> emptyCore() {
  auto core = std::make_unique<Core>();
  core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core->rsub.projParams.setGeomScreen(341.0f);
  core->mem_w32(kQueue, 0u);
  core->mem_w32(kShadowCursor, 0x80072500u);
  return core;
}

void test_empty_queue_is_atomic_valid_input() {
  auto core = emptyCore();
  spyro::field_shaded_queue_scene::Frame frame{};
  CHECK(spyro::field_shaded_queue_scene::prepare(core.get(), 512, frame) ==
        spyro::field_shaded_queue_scene::Status::Ready);
  const auto recipe = spyro::field_shaded_queue_recipe::derive(frame.input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::ValidEmpty);
  CHECK_EQ(frame.queueRecords, 0u);
  CHECK_EQ(frame.input.records.size(), 0u);
}

void test_invalid_actor_refuses_without_guest_side_effects() {
  auto core = emptyCore();
  core->mem_w32(kQueue, 0x807ffff0u);
  core->mem_w32(kQueue + 4u, 0u);
  spyro::field_shaded_queue_scene::Frame frame{};
  CHECK(spyro::field_shaded_queue_scene::prepare(core.get(), 512, frame) ==
        spyro::field_shaded_queue_scene::Status::InvalidActor);
  CHECK_EQ(core->mem_r32(kShadowCursor), 0x80072500u);
}

void inspectSnapshotIfRequested() {
  const char *path = std::getenv("PSXPORT_FIELD_SHADED_SNAPSHOT");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  auto core = emptyCore();
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  input.read(reinterpret_cast<char *>(core->ram), sizeof(core->ram));
  CHECK(input.gcount() == static_cast<std::streamsize>(sizeof(core->ram)));
  core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core->rsub.projParams.setGeomScreen(341.0f);

  spyro::field_shaded_queue_scene::Frame frame{};
  const auto scene = spyro::field_shaded_queue_scene::prepare(core.get(), 512, frame);
  const auto recipe = spyro::field_shaded_queue_recipe::derive(frame.input);
  const std::set<uint16_t> meshes(frame.sourceMeshIndices.begin(), frame.sourceMeshIndices.end());
  const std::set<int32_t> lightingOffsets(frame.sourceLightingOffsets.begin(),
                                          frame.sourceLightingOffsets.end());
  std::printf("field shaded snapshot: scene=%s queue=%u screen=%u valid_mesh=%u null=%u "
              "source_candidates=%u visible=%zu culled=%u visible_candidates=%u recipe=%u "
              "recipe_candidates=%u rejected=%u faces=%zu meshes=%zu lighting=%zu shadows=%zu\n",
              spyro::field_shaded_queue_scene::statusName(scene),
              frame.queueRecords,
              frame.screenRecords,
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
  RUN(empty_queue_is_atomic_valid_input);
  RUN(invalid_actor_refuses_without_guest_side_effects);
  inspectSnapshotIfRequested();
  return pt_summary();
}
