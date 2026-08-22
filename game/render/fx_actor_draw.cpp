#include "fx_actor_draw.h"

#include "actor_global_order.h"
#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"
#include "actor_scene_oracle.h"
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "painter_submission_preflight.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <vector>

namespace {

constexpr uint32_t kProducerKey = 0x8001F798u;

struct Material {
  bool textured = false;
  uint16_t clut = 0;
  uint16_t tpage = 0;
  std::array<uint32_t, 4> attributes{};
};

uint32_t vertex_count(spyro::actor_draw_recipe::Family family) {
  using Family = spyro::actor_draw_recipe::Family;
  return family == Family::G4 || family == Family::GT4 ? 4u : 3u;
}

std::array<uint32_t, 4> vertex_order(const spyro::actor_draw_recipe::Face &face) {
  std::array<uint32_t, 4> order{0, 1, 2, 3};
  if (face.origin == spyro::actor_draw_recipe::Origin::QuadSecond) {
    order[0] = 3;
  }
  return order;
}

Material material_for(const spyro::actor_draw_recipe::Face &face) {
  using Family = spyro::actor_draw_recipe::Family;
  using Origin = spyro::actor_draw_recipe::Origin;
  Material material{};
  material.textured = face.family == Family::GT3 || face.family == Family::GT4;
  if (!material.textured) {
    return material;
  }
  const auto &input = face.input;
  const bool sourceQuad = (int32_t)input.words[0] < 0;
  if (face.family == Family::GT4) {
    material.attributes = {
        input.words[3] + input.fog, input.words[4], input.words[5], input.words[5] >> 16};
  } else if (!sourceQuad) {
    material.attributes = {input.words[2] + input.fog, input.words[3], input.words[4], 0};
  } else if (face.origin == Origin::QuadSecond) {
    material.attributes = {(input.words[3] + input.fog) & 0xffff0000u | (input.words[5] >> 16),
                           input.words[4],
                           input.words[5],
                           0};
  } else {
    material.attributes = {input.words[3] + input.fog, input.words[4], input.words[5], 0};
  }
  material.clut = (uint16_t)(material.attributes[0] >> 16);
  material.tpage = (uint16_t)(material.attributes[1] >> 16);
  return material;
}

bool preflight_materials(const spyro::actor_draw_recipe::Recipe &recipe) {
  for (const auto &face : recipe.faces) {
    const Material material = material_for(face);
    if (material.textured && ((material.tpage >> 7) & 3u) > 2u) {
      return false;
    }
  }
  return true;
}

} // namespace

bool spyro_actor_submit(Core *c) {
  std::vector<spyro::actor_recipe_capture::Record> retailRecords;
  const auto oracleStatus = spyro::actor_scene_oracle::capture(c, retailRecords);
  if (oracleStatus == spyro::actor_scene_oracle::Status::Refused) {
    return false;
  }
  std::vector<spyro::actor_recipe_capture::Record> records;
  spyro::actor_scene::Census census{};
  const auto sceneStatus = spyro::actor_scene::build_records(c, records, census);
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
    lucent::debug(
        "actordirect",
        "REFUSED recipe={} records={} source_scanned={} source_queued={} source_culled={} "
        "coarse={} view={} invalid_model={}",
        (uint32_t)recipe.status,
        records.size(),
        census.scanned,
        census.queued,
        census.culled,
        census.coarseCulled,
        census.viewCulled,
        census.invalidModel);
    return false;
  }
  if (!preflight_materials(recipe)) {
    lucent::debug("actordirect", "REFUSED unsupported texture mode");
    return false;
  }
  const auto replay = spyro::actor_global_order::build(outputs, recipe.faces);
  if (replay.status != spyro::actor_global_order::Status::Ready) {
    lucent::debug("actordirect", "REFUSED global replay order={}", replay.refusal);
    return false;
  }
  RenderQueue &queue = c->game->rq;
  const auto plan = spyro::painter_submission::preflight(
      queue, kProducerKey, recipe.faces.size(), spyro::scene_painter_order::kStage13Domain);
  if (!plan.ready) {
    return false;
  }
  const GpuState gpu = c->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(c)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(c) - 1);
  }
  ProducerScope producer(&c->rsub.producerScope, kProducerKey, "actor:opaque");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (const auto &replayFace : replay.faces) {
    const auto &face = recipe.faces[replayFace.faceIndex];
    const uint32_t count = vertex_count(face.family);
    const auto order = vertex_order(face);
    const Material material = material_for(face);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t source = order[i];
      xs[i] = (int16_t)face.input.xy[source] + gpu.s_off_x;
      ys[i] = (int16_t)(face.input.xy[source] >> 16) + gpu.s_off_y;
      screenX[i] = face.input.screenX[source] + (float)gpu.s_off_x;
      screenY[i] = face.input.screenY[source] + (float)gpu.s_off_y;
      us[i] = material.attributes[i] & 0xffu;
      vs[i] = (material.attributes[i] >> 8) & 0xffu;
      const uint32_t rgb = face.input.color[source];
      red[i] = rgb;
      green[i] = rgb >> 8;
      blue[i] = rgb >> 16;
      depth[i] = c->rsub.projParams.pzToOrd(face.input.viewZ[source]);
    }
    queue.emitOrQueue(c,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      (int)count,
                      0,
                      0,
                      xs,
                      ys,
                      screenX,
                      screenY,
                      us,
                      vs,
                      red,
                      green,
                      blue,
                      depth,
                      material.textured ? (material.tpage >> 7) & 3u : 3u,
                      material.textured ? (material.tpage & 0x0fu) * 64 : 0,
                      material.textured ? ((material.tpage >> 4) & 1u) * 256 : 0,
                      material.textured ? (material.clut & 0x3fu) * 16 : 0,
                      material.textured ? (material.clut >> 6) & 0x1ffu : 0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      material.textured ? (material.tpage >> 5) & 3u : 0,
                      nullptr,
                      -1,
                      0.0f,
                      1,
                      material.textured ? (material.tpage >> 9) & 1u : gpu.s_tp_dither,
                      spyro::scene_painter_order::actor(
                          replayFace.otBin, replayFace.recordOrdinal, replayFace.chainOrdinal));
  }
  lucent::debug("actordirect",
                "PASS records={} candidates={} rejected={} faces={} painters_before={}",
                recipe.records,
                recipe.candidates,
                recipe.rejectedCandidates,
                recipe.faces.size(),
                plan.existingObjects);
  lucent::debug("actordirect",
                "source scanned={} queued={} culled={}",
                census.scanned,
                census.queued,
                census.culled);
  return true;
}
