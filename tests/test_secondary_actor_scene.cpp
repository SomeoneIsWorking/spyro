#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "core.h"
#include "secondary_actor_recipe.h"
#include "secondary_actor_scene.h"
#include "testutil.h"

#include <cstdlib>
#include <fstream>
#include <memory>

namespace {

constexpr uint32_t kSourceList = 0x80071ef4u;
constexpr uint32_t kShadowCursor = 0x80075f00u;

std::unique_ptr<Core> empty_core() {
  auto core = std::make_unique<Core>();
  core->mem_w32(kSourceList, 0u);
  core->mem_w32(kShadowCursor, 0x800724f4u);
  core->rsub.projParams.setGeomOffset(160.0f, 120.0f);
  core->rsub.projParams.setGeomScreen(350.0f);
  return core;
}

void test_empty_list_is_a_complete_atomic_frame() {
  auto core = empty_core();
  spyro::secondary_actor_scene::Frame frame{};
  CHECK(spyro::secondary_actor_scene::prepare(core.get(), frame) ==
        spyro::secondary_actor_scene::Status::Ready);
  CHECK_EQ(frame.visitedMobys.size(), 0u);
  CHECK_EQ(frame.records.size(), 0u);
  CHECK_EQ(frame.shadows.size(), 0u);

  std::vector<spyro::actor_recipe_capture::Record> records;
  std::vector<spyro::actor_prefix::Output> outputs;
  const auto recipe = spyro::actor_recipe_capture::compose_records(records, outputs);
  CHECK(recipe.status == spyro::actor_draw_recipe::Status::ValidEmpty);
  CHECK(outputs.empty());
}

void test_invalid_source_refuses_without_side_effects() {
  auto core = empty_core();
  core->mem_w32(kSourceList, 0x807ffff0u);
  spyro::secondary_actor_scene::Frame frame{};
  CHECK(spyro::secondary_actor_scene::prepare(core.get(), frame) ==
        spyro::secondary_actor_scene::Status::InvalidSourceList);
  CHECK_EQ(core->mem_r32(kSourceList), 0x807ffff0u);
  CHECK_EQ(core->mem_r32(kShadowCursor), 0x800724f4u);
}

void test_regular_descriptor_material_arms_preserve_binary_pairs() {
  using spyro::actor_recipe_capture::descriptorMaterial;
  const auto high = descriptorMaterial(spyro::actor_prefix::ColorArm::High);
  const auto positive = descriptorMaterial(spyro::actor_prefix::ColorArm::PositiveBlend);
  const auto plain = descriptorMaterial(spyro::actor_prefix::ColorArm::Plain);
  const auto negative = descriptorMaterial(spyro::actor_prefix::ColorArm::NegativeBlend);
  CHECK_EQ(high.commandOffset, 20u);
  CHECK_EQ(high.colorOffset, 24u);
  CHECK(!high.writesScratchColors);
  CHECK_EQ(positive.commandOffset, 20u);
  CHECK_EQ(positive.colorOffset, 24u);
  CHECK(positive.writesScratchColors);
  CHECK_EQ(plain.commandOffset, 28u);
  CHECK_EQ(plain.colorOffset, 32u);
  CHECK(!plain.writesScratchColors);
  CHECK_EQ(negative.commandOffset, 28u);
  CHECK_EQ(negative.colorOffset, 32u);
  CHECK(negative.writesScratchColors);
}

void inspect_snapshot_if_requested() {
  const char *path = std::getenv("PSXPORT_SECONDARY_SNAPSHOT");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  auto core = empty_core();
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  input.read(reinterpret_cast<char *>(core->ram), sizeof(core->ram));
  CHECK(input.gcount() == static_cast<std::streamsize>(sizeof(core->ram)));

  spyro::secondary_actor_scene::Frame frame{};
  std::vector<spyro::actor_recipe_capture::Record> regularRecords;
  spyro::actor_scene::Census regularCensus{};
  const auto regularScene =
      spyro::actor_scene::build_records(core.get(), regularRecords, regularCensus);
  std::vector<spyro::actor_prefix::Output> regularOutputs;
  const auto regularRecipe =
      spyro::actor_recipe_capture::compose_records(regularRecords, regularOutputs);
  const auto sceneStatus = spyro::secondary_actor_scene::prepare(core.get(), frame);
  const auto recipe = spyro::secondary_actor_recipe::derive(frame);
  const uint32_t firstRegularPrefix =
      regularRecipe.firstUnsupportedRecord < regularOutputs.size()
          ? static_cast<uint32_t>(regularOutputs[regularRecipe.firstUnsupportedRecord].status)
          : UINT32_MAX;
  std::printf("regular snapshot: scene=%s scanned=%u queued=%u culled=%u coarse=%u view=%u "
              "invalid=%u recipe=%u reason=%u records=%u candidates=%u rejected=%u faces=%zu "
              "unsupported_record=%u unsupported_word=%u prefix=%u\n",
              spyro::actor_scene::status_name(regularScene),
              regularCensus.scanned,
              regularCensus.queued,
              regularCensus.culled,
              regularCensus.coarseCulled,
              regularCensus.viewCulled,
              regularCensus.invalidModel,
              static_cast<unsigned>(regularRecipe.status),
              static_cast<unsigned>(regularRecipe.firstReason),
              regularRecipe.records,
              regularRecipe.candidates,
              regularRecipe.rejectedCandidates,
              regularRecipe.faces.size(),
              regularRecipe.firstUnsupportedRecord,
              regularRecipe.firstUnsupportedSourceWord,
              firstRegularPrefix);
  std::printf("secondary snapshot: scene=%s visited=%zu records=%zu shadows=%zu "
              "scanned=%u queued=%u culled=%u coarse=%u view=%u invalid=%u "
              "recipe=%u candidates=%u rejected=%u faces=%zu unsupported_record=%u "
              "unsupported_word=%u\n",
              spyro::secondary_actor_scene::status_name(sceneStatus),
              frame.visitedMobys.size(),
              frame.records.size(),
              frame.shadows.size(),
              frame.census.scanned,
              frame.census.queued,
              frame.census.culled,
              frame.census.coarseCulled,
              frame.census.viewCulled,
              frame.census.invalidModel,
              static_cast<unsigned>(recipe.status),
              recipe.candidates,
              recipe.rejectedCandidates,
              recipe.faces.size(),
              recipe.firstUnsupportedRecord,
              recipe.firstUnsupportedSourceWord);
  for (uint32_t i = 0; i < frame.records.size(); ++i) {
    const auto &record = frame.records[i];
    std::printf("  record[%u] moby=0x%08x lighting=0x%08x cr29=%d cr30=%d arm=%u "
                "prefix=%u words=%zu verts=%zu "
                "colors=%zu controls13=%u controls14=%u\n",
                i,
                record.moby,
                record.lightingControl,
                record.actor.input.cr29,
                record.actor.input.cr30,
                static_cast<unsigned>(record.actor.input.colorArm),
                static_cast<unsigned>(record.actor.expected.status),
                record.actor.expected.primitiveWords.size(),
                record.actor.expected.vertices.size(),
                record.actor.expected.colors.size(),
                record.actor.expected.controls[13],
                record.actor.expected.controls[14]);
  }
  CHECK(sceneStatus == spyro::secondary_actor_scene::Status::Ready);
  CHECK(regularScene == spyro::actor_scene::Status::Ready);
  CHECK_EQ(regularCensus.scanned, 175u);
  CHECK_EQ(regularCensus.queued, 14u);
  CHECK(regularRecipe.status == spyro::actor_draw_recipe::Status::Ready);
  CHECK_EQ(regularRecipe.candidates, 423u);
  CHECK_EQ(regularRecipe.rejectedCandidates, 211u);
  CHECK_EQ(regularRecipe.faces.size(), 212u);
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::Ready);
  CHECK_EQ(frame.visitedMobys.size(), 3u);
  CHECK_EQ(frame.records.size(), 1u);
  CHECK_EQ(recipe.candidates, 138u);
  CHECK_EQ(recipe.rejectedCandidates, 63u);
  CHECK_EQ(recipe.faces.size(), 75u);
}

} // namespace

int main() {
  RUN(empty_list_is_a_complete_atomic_frame);
  RUN(invalid_source_refuses_without_side_effects);
  RUN(regular_descriptor_material_arms_preserve_binary_pairs);
  inspect_snapshot_if_requested();
  return pt_summary();
}
