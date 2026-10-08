#include "core.h"
#include "field_environment_recipe.h"
#include "field_environment_scene.h"
#include "game.h"
#include "testutil.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

namespace {

void test_invalid_core_refuses() {
  spyro::field_environment_scene::Frame frame{};
  CHECK(spyro::field_environment_scene::prepare(nullptr, frame) ==
        spyro::field_environment_scene::Status::InvalidCore);
}

void inspectSnapshotIfRequested() {
  const char *path = std::getenv("PSXPORT_FIELD_ENVIRONMENT_SNAPSHOT");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  auto core = std::make_unique<Core>();
  auto game = std::make_unique<Game>();
  core->game = game.get();
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  input.read(reinterpret_cast<char *>(core->ram), sizeof(core->ram));
  CHECK(input.gcount() == static_cast<std::streamsize>(sizeof(core->ram)));
  core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core->rsub.projParams.setGeomScreen(341.0f);
  const uint32_t cullingBefore = core->mem_r32(spyro::field_environment::kCullingDistance);
  std::vector<uint8_t> edgeBefore(spyro::field_environment::kEdgeWorkAreaSize);
  for (uint32_t i = 0; i < edgeBefore.size(); ++i) {
    edgeBefore[i] = core->mem_r8(spyro::field_environment::kEdgeWorkArea + i);
  }

  spyro::field_environment_scene::Frame frame{};
  const auto status = spyro::field_environment_scene::prepare(core.get(), frame);
  std::printf("field environment snapshot: scene=%s selection=%d distance=0x%X sectors=%u "
              "low=%u high=%u candidates=%u rejected=%u faces=%zu status=%u reason=%s\n",
              spyro::field_environment_scene::statusName(status),
              frame.invocation.worldSelection,
              frame.invocation.cullingDistance,
              frame.world.selectedSectors,
              frame.world.lowSectors,
              frame.world.highSectors,
              frame.world.candidates,
              frame.world.rejected,
              frame.world.faces.size(),
              (uint32_t)frame.world.status,
              frame.world.refusal);
  CHECK_EQ(core->mem_r32(spyro::field_environment::kCullingDistance), cullingBefore);
  for (uint32_t i = 0; i < edgeBefore.size(); ++i) {
    CHECK_EQ(core->mem_r8(spyro::field_environment::kEdgeWorkArea + i), edgeBefore[i]);
  }
  CHECK(status == spyro::field_environment_scene::Status::Ready);
  CHECK_EQ(frame.invocation.worldSelection, 17);
  CHECK_EQ(frame.invocation.cullingDistance, 0x28000u);
  CHECK_EQ(frame.world.selectedSectors, 86u);
  CHECK_EQ(frame.world.lowSectors, 20u);
  CHECK_EQ(frame.world.highSectors, 29u);
  CHECK_EQ(frame.world.candidates, 1376u);
  CHECK_EQ(frame.world.rejected, 1039u);
  CHECK_EQ(frame.world.faces.size(), 413u);
}

} // namespace

int main() {
  RUN(invalid_core_refuses);
  inspectSnapshotIfRequested();
  return pt_summary();
}
