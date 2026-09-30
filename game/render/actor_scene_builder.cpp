#include "actor_scene_builder.h"
#include "guest_globals.h"

#include "actor_transform_math.h"
#include "core.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_scene {
namespace {

using spyro::guest::kLevelMobys;
constexpr uint32_t kModels = 0x80076378u;
using spyro::guest::kCamera;
constexpr uint32_t kMobySize = 0x58u;
constexpr uint32_t kMaxMobys = 4096u;
// g_SonyImage.u.m_Draw.m_Moby holds 0x240 pointers; a list that is not terminated inside it is a
// corrupt input rather than a longer list.
constexpr uint32_t kMaxDrawList = 0x240u;
constexpr uint32_t kWasDrawn = 0x51u;
constexpr uint32_t kShadowDistance = 0x1Cu;

using actor_transform_math::Matrix;

// 0x800521C0's list admission, before its category test: a live, positive-state regular Moby.
bool regular_list_candidate(uint32_t state) {
  const int8_t kind = (int8_t)(state >> 24);
  return kind != 0 && (int32_t)state >= 0;
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

bool build_source(Core *c,
                  uint32_t moby,
                  const Matrix &cameraMatrix,
                  int32_t drawWidth,
                  actor_recipe_capture::SourceRecord &source,
                  Census &census,
                  Answers &answers) {
  answers = {};
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
  source.descriptor = descriptor;
  source.tz = view[2];
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
  answers = classify_view(view, c->mem_r8(descriptor + 7u), radius, drawWidth);
  if (!answers.drawn.visible) {
    ++census.viewCulled;
    return false;
  }
  source.moby = moby;
  const uint32_t animation = c->mem_r32(moby + 68u);
  const uint32_t blend = c->mem_r8(moby + 64u);
  source.header = answers.drawn.flags + blend * 0x100u + (animation >> 24) * 0x10000u +
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

// Who asked for this entry: the guest's own list (retail runs 0x8001F158 over it and publishes
// its answers), the port's drawn list, or both. An explicit list is both by definition.
struct Membership {
  bool guest = false;
  bool drawn = false;
};

} // namespace

Answers classify_view(std::array<int32_t, 3> view,
                      uint32_t modelRadius,
                      int32_t radius,
                      int32_t drawWidth) {
  const int32_t extent = (int32_t)modelRadius * 16;
  const int32_t horizontalNear = extent / 2 + extent / 4 + extent / 32;
  const int32_t horizontalFar = extent / 2 + (int32_t)modelRadius + extent / 32;
  const int32_t verticalNear = extent / 4 + (int32_t)modelRadius;
  const int32_t verticalFar = extent - extent / 32 - extent / 64;
  const int32_t x = view[0], y = view[1], z = view[2];
  const bool depth = z - radius < 0 && z + extent > 0;
  const bool vertical = z + verticalNear - (std::abs(y) - verticalFar) * 3 >= 1;
  const bool whollyVertical = z - verticalNear - (std::abs(y) + verticalFar) * 3 >= 1;
  const auto answer = [&](bool horizontal, bool whollyHorizontal) {
    Visibility out{};
    out.horizontal = depth && horizontal;
    out.visible = out.horizontal && vertical;
    if (out.visible) {
      out.flags = whollyHorizontal && whollyVertical ? 0x40000000u : 0x80000000u;
    }
    return out;
  };
  const int32_t nearExtent = std::abs(x) - horizontalNear, nearDepth = z + horizontalFar;
  const int32_t farExtent = std::abs(x) + horizontalNear, farDepth = z - horizontalFar;
  return {.guest = answer(wide::viewHorizontalInside(nearExtent, nearDepth, wide::kNativeClipWidth),
                          wide::viewHorizontalInside(farExtent, farDepth, wide::kNativeClipWidth)),
          .drawn = answer(wide::drawnHorizontalInside(nearExtent, nearDepth, drawWidth),
                          wide::viewHorizontalInside(farExtent, farDepth, drawWidth))};
}

bool stages_shadow(int32_t shadowWord, int32_t viewZ, int32_t limit) {
  return shadowWord < 0 && viewZ < limit;
}

std::optional<moby_shadow_list::Entry> shadow_entry(Core *c, uint32_t moby, uint32_t descriptor) {
  // 0x8001F358..0x8001F36C: descriptor + 0x24 + 6 + m_ShadowIndex * 8, one byte.
  const uint32_t texture = descriptor + 0x2Au + (uint32_t)c->mem_r8(moby + 0x3Eu) * 8u;
  if (!actor_recipe_capture::physical_span(texture & ~3u, 4u)) {
    return std::nullopt;
  }
  return moby_shadow_list::Entry{.moby = moby, .radius = c->mem_r8(texture)};
}

bool build_source_record(Core *c,
                         uint32_t moby,
                         int32_t drawWidth,
                         actor_recipe_capture::SourceRecord &source,
                         Census &census,
                         Answers &answers) {
  return build_source(
      c, moby, actor_transform_math::readCameraMatrix(c), drawWidth, source, census, answers);
}

namespace {

// One entry's worth of the pass, shared by the level scan and the explicit list so their culling,
// capture and shadow staging cannot drift apart.
Status capture_entry(Core *c,
                     uint32_t moby,
                     const Matrix &cameraMatrix,
                     int32_t drawWidth,
                     bool captureShadows,
                     Membership membership,
                     Frame &frame) {
  actor_recipe_capture::SourceRecord source{};
  Answers answers{};
  const bool drawn =
      build_source(c, moby, cameraMatrix, drawWidth, source, frame.census, answers) &&
      membership.drawn;
  if (membership.guest) {
    frame.wasDrawn.push_back(
        {.moby = moby, .value = static_cast<uint8_t>(answers.guest.visible ? 1u : 0u)});
  }
  // 0x8001F344 stages the shadow after the horizontal plane and before the vertical one, so an
  // entry the vertical plane culls still casts one.
  const int32_t shadowWord = (int32_t)c->mem_r32(moby + kShadowDistance);
  const bool stages = captureShadows && stages_shadow(shadowWord, source.tz, kShadowStagingDepth);
  if (stages && membership.guest && answers.guest.horizontal) {
    const auto entry = shadow_entry(c, moby, source.descriptor);
    if (!entry || !actor_recipe_capture::physical_span(
                      frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
      return Status::InvalidShadowCursor;
    }
    frame.shadows.push_back({.moby = moby, .modelByte = entry->radius});
  }
  if (stages && membership.drawn && answers.drawn.horizontal) {
    const auto entry = shadow_entry(c, moby, source.descriptor);
    if (!entry) {
      return Status::InvalidShadowCursor;
    }
    frame.drawnShadows.push_back(*entry);
  }
  if (!drawn) {
    ++frame.census.culled;
    return Status::Ready;
  }
  if (frame.records.size() == actor_recipe_capture::kDurableRecords) {
    return Status::RecordCapacityExceeded;
  }
  actor_recipe_capture::Record record{};
  if (!actor_recipe_capture::capture_source(c, source, record)) {
    return Status::RecordCaptureRefused;
  }
  frame.records.push_back(std::move(record));
  ++frame.census.queued;
  return Status::Ready;
}

Status scan_level_array(Core *c,
                        const Matrix &cameraMatrix,
                        const DrawnScope &drawn,
                        bool captureShadows,
                        Frame &frame) {
  const uint32_t first = c->mem_r32(kLevelMobys);
  if (!actor_recipe_capture::physical_span(first, kMobySize)) {
    return Status::InvalidMobyArray;
  }
  // 0x800521C0 classified this frame's list from the table as it stood, which is what it reads.
  const sector_visibility::Table guestSectors = sector_visibility::readGuest(*c);
  for (uint32_t i = 0, moby = first; i < kMaxMobys; ++i, moby += kMobySize) {
    if (!actor_recipe_capture::physical_span(moby, kMobySize)) {
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
    if (!regular_list_candidate(state)) {
      continue;
    }
    const uint32_t category = (state >> 16) & 0xffu;
    const Membership membership{.guest = sector_visibility::categoryVisible(guestSectors, category),
                                .drawn =
                                    sector_visibility::categoryVisible(drawn.sectors, category)};
    if (!membership.guest && !membership.drawn) {
      continue;
    }
    const Status status =
        capture_entry(c, moby, cameraMatrix, drawn.width, captureShadows, membership, frame);
    if (status != Status::Ready) {
      return status;
    }
  }
  return Status::UnterminatedMobyArray;
}

Status walk_explicit_list(Core *c,
                          uint32_t list,
                          const Matrix &cameraMatrix,
                          int32_t drawWidth,
                          bool captureShadows,
                          Frame &frame) {
  for (uint32_t i = 0; i < kMaxDrawList; ++i) {
    if (!actor_recipe_capture::physical_span(list + i * 4u, 4u)) {
      return Status::InvalidMobyArray;
    }
    const uint32_t moby = c->mem_r32(list + i * 4u);
    if (moby == 0u) {
      return Status::Ready;
    }
    if (!actor_recipe_capture::physical_span(moby, kMobySize)) {
      return Status::InvalidMobyArray;
    }
    ++frame.census.scanned;
    const Status status = capture_entry(
        c, moby, cameraMatrix, drawWidth, captureShadows, {.guest = true, .drawn = true}, frame);
    if (status != Status::Ready) {
      return status;
    }
  }
  return Status::UnterminatedMobyArray;
}

Status
build_scene(Core *c, Frame &frame, bool captureShadows, const DrawnScope &drawn, Source source) {
  frame = {};
  if (captureShadows) {
    // 0x8001F158 resets the temporary shadow cursor to the fixed list start on every call. The
    // later secondary/shaded passes consume the cursor it publishes at 0x80075F00.
    frame.shadowCursor = moby_shadow_list::kStart;
  }
  if (captureShadows && !actor_recipe_capture::physical_span(frame.shadowCursor, 8u)) {
    return Status::InvalidShadowCursor;
  }
  const Matrix cameraMatrix = actor_transform_math::readCameraMatrix(c);
  const Status status =
      source.kind == Source::Kind::ExplicitList
          ? walk_explicit_list(c, source.list, cameraMatrix, drawn.width, captureShadows, frame)
          : scan_level_array(c, cameraMatrix, drawn, captureShadows, frame);
  if (status != Status::Ready) {
    frame = {};
  }
  return status;
}

} // namespace

Status build_frame(Core *c, Frame &frame, const DrawnScope &drawn, Source source) {
  if (c == nullptr) {
    frame = {};
    return Status::InvalidMobyArray;
  }
  return build_scene(c, frame, true, drawn, source);
}

void commit(Core *c, const Frame &frame) {
  for (const WasDrawn &entry : frame.wasDrawn) {
    c->mem_w8(entry.moby + kWasDrawn, entry.value);
  }
  for (uint32_t i = 0; i < frame.shadows.size(); ++i) {
    const uint32_t out = frame.shadowCursor + i * 8u;
    c->mem_w32(out, frame.shadows[i].moby);
    c->mem_w32(out + 4u, frame.shadows[i].modelByte);
  }
  c->mem_w32(moby_shadow_list::kCursor, frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
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
  const Status status =
      build_scene(c, frame, false, {.sectors = sector_visibility::readGuest(*c)}, {});
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
