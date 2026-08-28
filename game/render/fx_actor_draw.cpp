#include "fx_actor_draw.h"

#include "actor_face_submitter.h"
#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "actor_scene_oracle.h"
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdint>
#include <lucent/log.h>
#include <vector>

namespace {

constexpr uint32_t kProducerKey = 0x8001F798u;

} // namespace

bool spyro_actor_submit(Core *c) {
  std::vector<spyro::actor_recipe_capture::Record> retailRecords;
  const auto oracleStatus = spyro::actor_scene_oracle::capture(c, retailRecords);
  if (oracleStatus == spyro::actor_scene_oracle::Status::Refused) {
    return false;
  }
  spyro::actor_scene::Frame sceneFrame{};
  const auto sceneStatus = spyro::actor_scene::build_frame(c, sceneFrame);
  auto &records = sceneFrame.records;
  const auto &census = sceneFrame.census;
  if (sceneStatus != spyro::actor_scene::Status::Ready) {
    lucent::debug(
        "actordirect",
        "REFUSED scene={} scanned={} queued={} culled={} coarse={} view={} invalid_model={}",
        spyro::actor_scene::status_name(sceneStatus),
        census.scanned,
        census.queued,
        census.culled,
        census.coarseCulled,
        census.viewCulled,
        census.invalidModel);
    return false;
  }
  if (oracleStatus == spyro::actor_scene_oracle::Status::Captured &&
      !spyro::actor_scene_oracle::compare(retailRecords, records)) {
    return false;
  }
  for (uint32_t index = 0; index < records.size(); ++index) {
    const auto &input = records[index].input;
    lucent::debug("actordirect",
                  "semantic record={} view=({},{},{}) vertices={} header=0x{:08X} "
                  "matrix={:08X},{:08X},{:08X},{:08X},{:08X}",
                  index,
                  input.tx,
                  input.ty,
                  input.tz,
                  input.vertexCount,
                  input.header,
                  input.matrixWords[0],
                  input.matrixWords[1],
                  input.matrixWords[2],
                  input.matrixWords[3],
                  input.matrixWords[4]);
  }
  if (gpu_vk_wide_engine(c)) {
    const int32_t center = gpu_vk_wide_engine_w(c) / 2;
    for (auto &record : records) {
      record.input.projection.ofx = center << 16;
      record.expected = spyro::actor_prefix::build(record.input);
    }
  }
  std::vector<spyro::actor_prefix::Output> outputs;
  const auto recipe = spyro::actor_recipe_capture::compose_records(records, outputs);
  if (recipe.status == spyro::actor_draw_recipe::Status::ValidEmpty) {
    return true;
  }
  if (recipe.status != spyro::actor_draw_recipe::Status::Ready) {
    const uint32_t firstPrefixStatus =
        outputs.empty() ? UINT32_MAX : (uint32_t)outputs.front().status;
    lucent::debug(
        "actordirect",
        "REFUSED recipe={} reason={} prefix_status={} record={} source_word={} words={:08X},{:08X} "
        "records={} candidates={} source_scanned={} source_queued={} source_culled={} coarse={} "
        "view={} invalid_model={}",
        (uint32_t)recipe.status,
        (uint32_t)recipe.firstReason,
        firstPrefixStatus,
        recipe.firstUnsupportedRecord,
        recipe.firstUnsupportedSourceWord,
        recipe.firstUnsupportedWords[0],
        recipe.firstUnsupportedWords[1],
        records.size(),
        recipe.candidates,
        census.scanned,
        census.queued,
        census.culled,
        census.coarseCulled,
        census.viewCulled,
        census.invalidModel);
    return false;
  }
  RenderQueue &queue = c->game->rq;
  const auto plan =
      spyro::actor_face_submitter::prepare(queue, kProducerKey, outputs, recipe.faces);
  if (plan.status != spyro::actor_face_submitter::Status::Ready) {
    lucent::debug("actordirect", "REFUSED submission={}", (uint32_t)plan.status);
    return false;
  }
  const GpuState gpu = c->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "actor:opaque");
  spyro::actor_face_submitter::submit(
      c, queue, kProducerKey, spyro::actor_face_submitter::Layer::Regular, recipe.faces, plan);
  spyro::actor_scene::commit(c, sceneFrame);
  lucent::debug("actordirect",
                "PASS records={} candidates={} rejected={} faces={} shadows={} painters_before={}",
                recipe.records,
                recipe.candidates,
                recipe.rejectedCandidates,
                recipe.faces.size(),
                sceneFrame.shadows.size(),
                plan.admission.existingObjects);
  lucent::debug("actordirect",
                "source scanned={} queued={} culled={}",
                census.scanned,
                census.queued,
                census.culled);
  return true;
}
