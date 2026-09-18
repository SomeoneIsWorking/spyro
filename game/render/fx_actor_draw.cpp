#include "fx_actor_draw.h"

#include "actor_emit.h"
#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "actor_temporal.h"
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "render_queue.h"
#include "spyro_context.h"

#include <cstdint>
#include <lucent/log.h>
#include <utility>
#include <vector>

bool spyro_actor_submit(Core *c, spyro::actor_scene::Source source) {
  spyro::actor_scene::Frame sceneFrame{};
  const auto sceneStatus = spyro::actor_scene::build_frame(c, sceneFrame, source);
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
  for (uint32_t index = 0; index < records.size(); ++index) {
    const auto &input = records[index].input;
    lucent::debug("actordirect",
                  "semantic record={} moby=0x{:08X} shadow_word=0x{:08X} view=({},{},{}) "
                  "vertices={} header=0x{:08X} matrix={:08X},{:08X},{:08X},{:08X},{:08X}",
                  index,
                  records[index].moby,
                  c->mem_r32(records[index].moby + 0x1Cu),
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
  const auto prepared =
      spyro::actor_emit::prepare(*c, c->game->rq, spyro::actor_draw::kProducerKey, records);
  const auto &recipe = prepared.recipe;
  if (prepared.status != spyro::actor_emit::Status::Ready &&
      prepared.status != spyro::actor_emit::Status::ValidEmpty) {
    const uint32_t firstPrefixStatus =
        prepared.outputs.empty() ? UINT32_MAX : (uint32_t)prepared.outputs.front().status;
    lucent::debug("actordirect",
                  "REFUSED stage={} recipe={} reason={} prefix_status={} submission={} record={} "
                  "source_word={} words={:08X},{:08X} records={} candidates={} source_scanned={} "
                  "source_queued={} source_culled={} coarse={} view={} invalid_model={}",
                  spyro::actor_emit::statusName(prepared.status),
                  (uint32_t)recipe.status,
                  (uint32_t)recipe.firstReason,
                  firstPrefixStatus,
                  spyro::actor_face_submitter::statusName(prepared.plan.status),
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
  // Preparation owns the shadow-list lifecycle even when every face is culled, and an empty picture
  // is still an endpoint: the next frame can interpolate against a scene that drew nothing.
  spyro::actor_emit::publish(
      *c, c->game->rq, spyro::actor_draw::kProducerKey, spyro::actor_draw::kProducerName, prepared);
  spyro::actor_scene::commit(c, sceneFrame);
  lucent::debug("actordirect",
                "PASS records={} candidates={} rejected={} faces={} shadows={} painters_before={}",
                recipe.records,
                recipe.candidates,
                recipe.rejectedCandidates,
                recipe.faces.size(),
                sceneFrame.shadows.size(),
                prepared.plan.admission.existingObjects);
  lucent::debug("actordirect",
                "source scanned={} queued={} culled={}",
                census.scanned,
                census.queued,
                census.culled);
  spyro_context(*c).actorTemporal.retain(std::move(records));
  return true;
}
