#include "actor_scene_builder.h"

#include "actor_transform_math.h"
#include "core.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_scene {
namespace {

constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kCategoryVisibility = 0x800771C8u;
constexpr uint32_t kModels = 0x80076378u;
constexpr uint32_t kCamera = 0x80076DD0u;
constexpr uint32_t kMobySize = 0x58u;
constexpr uint32_t kMaxMobys = 4096u;
constexpr uint32_t kShadowListStart = 0x800724F4u;
constexpr uint32_t kShadowCursor = 0x80075F00u;

using actor_transform_math::Matrix;

bool regular_list_member(Core *c, uint32_t state) {
  const int8_t kind = (int8_t)(state >> 24);
  if (kind == 0 || (int32_t)state < 0) {
    return false;
  }
  const uint32_t category = (state >> 16) & 0xffu;
  return category == 0xffu || c->mem_r8(kCategoryVisibility + category) > 0;
}

bool coarse_visible(Core *c, uint32_t moby, int32_t radius) {
  const int32_t cameraX = (int32_t)c->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)c->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)c->mem_r32(kCamera + 48u);
  const int32_t dx = ((int32_t)c->mem_r32(moby + 12u) - cameraX) >> 2;
  const int32_t dy = (cameraY - (int32_t)c->mem_r32(moby + 16u)) >> 2;
  const int32_t dz = (cameraZ - (int32_t)c->mem_r32(moby + 20u)) >> 2;
  return dx + radius > 0 && dx - radius < 0 && dy + radius > 0 && dy - radius < 0 &&
         dz + radius > 0 && dz - radius < 0;
}

bool view_visible(std::array<int32_t, 3> view,
                  uint32_t modelRadius,
                  int32_t radius,
                  uint32_t &flags) {
  const int32_t extent = (int32_t)modelRadius * 16;
  const int32_t horizontalNear = extent / 2 + extent / 4 + extent / 32;
  const int32_t horizontalFar = extent / 2 + (int32_t)modelRadius + extent / 32;
  const int32_t verticalNear = extent / 4 + (int32_t)modelRadius;
  const int32_t verticalFar = extent - extent / 32 - extent / 64;
  const int32_t x = view[0], y = view[1], z = view[2];
  if (z - radius >= 0 || z + extent <= 0 ||
      (std::abs(x) - horizontalNear) * 4 - (z + horizontalFar) * 3 >= 0 ||
      z + verticalNear - (std::abs(y) - verticalFar) * 3 < 1) {
    return false;
  }
  if ((std::abs(x) + horizontalNear) * 4 - (z - horizontalFar) * 3 < 0 &&
      z - verticalNear - (std::abs(y) + verticalFar) * 3 >= 1) {
    flags = 0x40000000u;
  } else {
    flags = 0x80000000u;
  }
  return true;
}

bool build_source(Core *c,
                  uint32_t moby,
                  const Matrix &cameraMatrix,
                  actor_recipe_capture::SourceRecord &source,
                  Census &census) {
  const uint16_t extentWord = c->mem_r16(moby + 80u);
  const int32_t radius = (int32_t)((extentWord & 0xffu) << 8) + (int32_t)(extentWord & 0x100u) * 2;
  if (!coarse_visible(c, moby, radius)) {
    ++census.coarseCulled;
    return false;
  }
  const uint32_t modelSet = c->mem_r32(kModels + c->mem_r16(moby + 54u) * 4u);
  const uint32_t frame = c->mem_r32(moby + 60u);
  const uint32_t descriptor = c->mem_r32(modelSet + (frame & 0xffu) * 4u + 56u);
  if (!actor_recipe_capture::physical_span(descriptor, 36u)) {
    ++census.invalidModel;
    return false;
  }
  const int32_t cameraX = (int32_t)c->mem_r32(kCamera + 40u);
  const int32_t cameraY = (int32_t)c->mem_r32(kCamera + 44u);
  const int32_t cameraZ = (int32_t)c->mem_r32(kCamera + 48u);
  const std::array<int32_t, 3> relative = {((int32_t)c->mem_r32(moby + 12u) - cameraX) >> 2,
                                           (cameraY - (int32_t)c->mem_r32(moby + 16u)) >> 2,
                                           (cameraZ - (int32_t)c->mem_r32(moby + 20u)) >> 2};
  // The handwritten MVMVA sequence loads IR1/IR2/IR3 as Y/Z/X, respectively. Preserve that
  // deliberate cyclic lane layout rather than pretending this is an ordinary XYZ matrix call.
  const std::array<int32_t, 3> view =
      actor_transform_math::transform(cameraMatrix, {relative[1], relative[2], relative[0]});
  lucent::debug("actordirect",
                "candidate moby=0x{:08X} relative=({},{},{}) view=({},{},{}) radius={} "
                "model_radius={}",
                moby,
                relative[0],
                relative[1],
                relative[2],
                view[0],
                view[1],
                view[2],
                radius,
                c->mem_r8(descriptor + 7u));
  uint32_t clipFlags = 0;
  if (!view_visible(view, c->mem_r8(descriptor + 7u), radius, clipFlags)) {
    ++census.viewCulled;
    return false;
  }
  const uint32_t animation = c->mem_r32(moby + 68u);
  const uint32_t blend = c->mem_r8(moby + 64u);
  source.header = clipFlags + blend * 0x100u + (animation >> 24) * 0x10000u +
                  (uint32_t)c->mem_r8(descriptor + 11u) * 0x1000000u + c->mem_r8(moby + 87u);
  source.descriptor = descriptor;
  source.model = descriptor + 36u + ((frame >> 16) & 0xffu) * 8u;
  if (blend != 0) {
    const uint32_t alternateDescriptor = c->mem_r32(modelSet + ((frame & 0xff00u) >> 6) + 56u);
    source.alternate = alternateDescriptor + 36u + (frame >> 24) * 8u;
  }
  source.tx = view[0];
  source.ty = view[1];
  source.tz = view[2];
  const Matrix actorMatrix = actor_transform_math::rotateForMoby(c, cameraMatrix, animation);
  const int16_t cr30 = (int16_t)((int32_t)(c->mem_r8(moby + 75u) & 0x3fu) * 256 - (int16_t)view[2]);
  source.matrixWords = actor_transform_math::packMatrix(actorMatrix, cr30);
  return true;
}

} // namespace

bool build_source_record(Core *c,
                         uint32_t moby,
                         actor_recipe_capture::SourceRecord &source,
                         Census &census) {
  return build_source(c, moby, actor_transform_math::readCameraMatrix(c), source, census);
}

Status build_scene(Core *c, Frame &frame, bool captureShadows) {
  frame = {};
  if (captureShadows) {
    // 0x8001F158 resets the temporary shadow cursor to the fixed list start on every call. The
    // later secondary/shaded passes consume the cursor it publishes at 0x80075F00.
    frame.shadowCursor = kShadowListStart;
  }
  if (captureShadows && !actor_recipe_capture::physical_span(frame.shadowCursor, 8u)) {
    return Status::InvalidShadowCursor;
  }
  const uint32_t first = c->mem_r32(kLevelMobys);
  if (!actor_recipe_capture::physical_span(first, kMobySize)) {
    return Status::InvalidMobyArray;
  }
  const Matrix cameraMatrix = actor_transform_math::readCameraMatrix(c);
  for (uint32_t i = 0, moby = first; i < kMaxMobys; ++i, moby += kMobySize) {
    if (!actor_recipe_capture::physical_span(moby, kMobySize)) {
      frame = {};
      return Status::InvalidMobyArray;
    }
    const uint32_t state = c->mem_r32(moby + 72u);
    if ((int8_t)state < 0) {
      if ((state & 0xffu) == 0xffu) {
        return Status::Ready;
      }
      continue;
    }
    ++frame.census.scanned;
    if (!regular_list_member(c, state)) {
      continue;
    }
    actor_recipe_capture::SourceRecord source{};
    if (!build_source(c, moby, cameraMatrix, source, frame.census)) {
      ++frame.census.culled;
      continue;
    }
    actor_recipe_capture::Record record{};
    if (frame.records.size() == actor_recipe_capture::kDurableRecords) {
      frame = {};
      return Status::RecordCapacityExceeded;
    }
    if (!actor_recipe_capture::capture_source(c, source, record)) {
      frame = {};
      return Status::RecordCaptureRefused;
    }
    frame.records.push_back(std::move(record));
    ++frame.census.queued;

    // 0x8001F158 appends this entry after the source has passed its projected cull. The model byte
    // is the same descriptor-relative shadow selector used by 0x800208FC, so both regular and
    // secondary paths name the identical AnimationFrame::m_Shadow byte.
    if (captureShadows && (int32_t)c->mem_r32(moby + 0x1Cu) < 0 && source.tz < -0x1200) {
      const uint32_t texture = source.descriptor + 0x2Au + (uint32_t)c->mem_r8(moby + 0x3Eu) * 8u;
      if (!actor_recipe_capture::physical_span(texture & ~3u, 4u) ||
          !actor_recipe_capture::physical_span(
              frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
        frame = {};
        return Status::InvalidShadowCursor;
      }
      frame.shadows.push_back({.moby = moby, .modelByte = c->mem_r8(texture)});
    }
  }
  frame = {};
  return Status::UnterminatedMobyArray;
}

Status build_frame(Core *c, Frame &frame) {
  if (c == nullptr) {
    frame = {};
    return Status::InvalidMobyArray;
  }
  const Status status = build_scene(c, frame, true);
  return status;
}

void commit(Core *c, const Frame &frame) {
  for (uint32_t i = 0; i < frame.shadows.size(); ++i) {
    const uint32_t out = frame.shadowCursor + i * 8u;
    c->mem_w32(out, frame.shadows[i].moby);
    c->mem_w32(out + 4u, frame.shadows[i].modelByte);
  }
  c->mem_w32(kShadowCursor, frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
}

Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census) {
  records.clear();
  census = {};
  if (c == nullptr) {
    return Status::InvalidMobyArray;
  }
  // Preserve the historical record-only helper contract for its unit callers. The shipping owner
  // uses build_frame so shadow state is staged from the same culling pass rather than rescanned.
  Frame frame{};
  const Status status = build_scene(c, frame, false);
  records = std::move(frame.records);
  census = frame.census;
  return status;
}

const char *status_name(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidMobyArray:
    return "invalid Moby array";
  case Status::UnterminatedMobyArray:
    return "unterminated Moby array";
  case Status::RecordCapacityExceeded:
    return "record capacity exceeded";
  case Status::RecordCaptureRefused:
    return "record capture refused";
  case Status::InvalidShadowCursor:
    return "invalid shadow cursor";
  }
  return "unknown";
}

} // namespace spyro::actor_scene
