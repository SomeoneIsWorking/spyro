#include "fx_field_tracers.h"

#include "core.h"
#include "field_tracers_recipe.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "render_queue.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x800189f0u;
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kLevelId = 0x8007596cu;
constexpr uint32_t kSqrtTable = 0x80074b84u;
constexpr uint32_t kTracerCount = 0x80075684u;

struct ProjectedPoint {
  uint32_t address = 0;
  int x = 0;
  int y = 0;
  int z = 0;
  int age = 0;
};

int tracerMagnitude(Core *core, uint32_t value) {
  if (value == 0u) {
    return 0;
  }
  const unsigned leading = (unsigned)__builtin_clz(value) & ~1u;
  const int exponent = (31 - (int)leading) >> 1;
  uint32_t normalized = 0;
  if (leading >= 24u) {
    normalized = value << (leading - 24u);
  } else {
    normalized = value >> (24u - leading);
  }
  const int32_t tableIndex = ((int32_t)(normalized - 0x40u)) << 1;
  const int32_t tableValue = core->mem_r16s(kSqrtTable + (uint32_t)tableIndex);
  return (int)((uint32_t)(tableValue << exponent) >> 12);
}

bool preflight(Core *core, const spyro::field_tracers_recipe::Recipe &recipe) {
  if (recipe.status != spyro::field_tracers_recipe::Status::Ready) {
    return recipe.status == spyro::field_tracers_recipe::Status::ValidEmpty;
  }
  size_t segmentCount = 0;
  for (const auto &chain : recipe.chains) {
    if (chain.points.size() > 1u) {
      segmentCount += chain.points.size() - 1u;
    }
  }
  const RenderQueue &queue = core->game->rq;
  const size_t queued = queue.consumed ? 0u : (size_t)queue.n;
  return segmentCount <= RQ_MAX - queued;
}

} // namespace

bool spyro_field_tracers_submit(Core *core) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const auto recipe = spyro::field_tracers_recipe::derive(ram);
  if (!preflight(core, recipe)) {
    lucent::debug("tracers",
                  "REFUSED status={} tracers={}",
                  spyro::field_tracers_recipe::statusName(recipe.status),
                  recipe.tracerCount);
    return false;
  }
  if (recipe.status == spyro::field_tracers_recipe::Status::ValidEmpty) {
    return true;
  }

  const auto camera = spyro::world_projection_math::decodeMatrix(ram, kCamera);
  psxport::native_projection::ProjectionParams projection{};
  projection.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  projection.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  projection.h = (uint16_t)core->rsub.projParams.geomH();
  if (gpu_vk_wide_engine(core)) {
    projection.ofx = (gpu_vk_wide_engine_w(core) / 2) << 16;
  }
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 0x2cu) >> 2;
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 0x30u) >> 2;
  std::vector<std::vector<ProjectedPoint>> projected;
  projected.reserve(recipe.chains.size());
  for (const auto &chain : recipe.chains) {
    auto &out = projected.emplace_back();
    out.reserve(chain.points.size());
    for (const auto &point : chain.points) {
      const auto input = spyro::world_projection_math::packProjectionInput(
          ((point.x - cameraX) >> 1), ((cameraY - point.y) >> 1), ((cameraZ - point.z) >> 1));
      const auto p = psxport::native_projection::project(camera, projection, input);
      const int x = p.sx;
      const int y = p.sy;
      const int z = p.ir[2];
      core->mem_w32(point.address + 12u, (uint32_t)x);
      core->mem_w32(point.address + 16u, (uint32_t)y);
      core->mem_w32(point.address + 20u, (uint32_t)z);
      out.push_back(ProjectedPoint{point.address, x, y, z, point.age});
    }
  }

  RenderQueue &queue = core->game->rq;
  const GpuState &gpu = core->game->gpu;
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "tracers");
  int pointCount = 0;
  int x2 = 0, x3 = 0, y2 = 0, y3 = 0;
  const bool mines = core->mem_r32(kLevelId) == 63u;
  for (const auto &chain : projected) {
    for (size_t j = 0; j + 1u < chain.size(); ++j) {
      int width = 70000;
      if (j + 2u == chain.size()) {
        width = 0;
      }
      if (j < 3u) {
        width = (int)j * 20000;
      }
      if (mines) {
        width >>= 2;
      }
      const auto &current = chain[j];
      if (current.age > 80) {
        continue;
      }
      ++pointCount;
      const auto &next = chain[j + 1u];
      const int deltaX = current.x - next.x;
      const int deltaY = current.y - next.y;
      const int rotatedX = deltaY;
      const int rotatedY = -deltaX;
      const uint32_t magnitudeSquared =
          (uint32_t)((int64_t)rotatedX * rotatedX + (int64_t)rotatedY * rotatedY);
      const int magnitude = tracerMagnitude(core, magnitudeSquared);
      int offsetX = 0;
      int offsetY = 0;
      if (magnitude != 0 && current.z != 0) {
        offsetX = (rotatedX * width) / (current.z * magnitude);
        offsetY = (rotatedY * width) / (current.z * magnitude);
      }
      const int x0 = current.x + offsetX;
      const int x1 = current.x - offsetX;
      const int y0 = current.y + offsetY;
      const int y1 = current.y - offsetY;
      if (pointCount > 1 && (current.z >> 7) < 2000) {
        int r1 = std::max(150 - 2 * current.age, 0);
        int g1 = std::max(150 - 4 * current.age, 0);
        int b1 = std::max(150 - 10 * current.age, 0);
        const int previousAge =
            j > 0u ? chain[j - 1u].age : (int32_t)core->mem_r32(current.address - 4u);
        int r2 = std::max(150 - 2 * previousAge, 0);
        int g2 = std::max(150 - 4 * previousAge, 0);
        int b2 = std::max(150 - 10 * previousAge, 0);
        if (mines) {
          std::swap(r1, g1);
          std::swap(r2, g2);
        }
        const int xs[4] = {x0, x1, x2, x3};
        const int ys[4] = {y0, y1, y2, y3};
        const int us[4] = {}, vs[4] = {};
        const unsigned char rs[4] = {(uint8_t)r1, (uint8_t)r1, (uint8_t)r2, (uint8_t)r2};
        const unsigned char gs[4] = {(uint8_t)g1, (uint8_t)g1, (uint8_t)g2, (uint8_t)g2};
        const unsigned char bs[4] = {(uint8_t)b1, (uint8_t)b1, (uint8_t)b2, (uint8_t)b2};
        // The guest inserts this G4 once at the current point's OT depth.  The host queue stores
        // one depth per vertex, so repeat that same OT depth rather than inventing a depth for the
        // prior segment's screen coordinates.
        const float depth = core->rsub.projParams.pzToOrd((float)current.z);
        const float depths[4] = {depth, depth, depth, depth};
        core->game->gpu.s_seen3d = 1;
        queue.emitOrQueue(core,
                          1,
                          RQ_WORLD,
                          RQ_OM_DEPTH,
                          4,
                          1,
                          0,
                          xs,
                          ys,
                          nullptr,
                          nullptr,
                          us,
                          vs,
                          rs,
                          gs,
                          bs,
                          depths,
                          3,
                          0,
                          0,
                          0,
                          0,
                          gpu.s_tw_mx,
                          gpu.s_tw_my,
                          gpu.s_tw_ox,
                          gpu.s_tw_oy,
                          gpu.s_da_x0,
                          gpu.s_da_y0,
                          gpu.s_da_x1,
                          gpu.s_da_y1,
                          0);
      }
      x2 = x0;
      x3 = x1;
      y2 = y0;
      y3 = y1;
    }
  }
  core->mem_w32(kTracerCount, 0u);
  lucent::debug("tracers", "PASS tracers={} segments={}", recipe.tracerCount, pointCount);
  return true;
}
