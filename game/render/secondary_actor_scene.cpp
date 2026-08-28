#include "secondary_actor_scene.h"

#include <utility>

#include "core.h"

#include <cstdint>

namespace spyro::secondary_actor_scene {
namespace {

constexpr uint32_t kSourceList = 0x80071ef4u;
constexpr uint32_t kSourceCapacity = 256u;
constexpr uint32_t kShadowCursor = 0x80075f00u;

} // namespace

Status prepare(Core *core, Frame &frame) {
  frame = {};
  frame.shadowCursor = core->mem_r32(kShadowCursor);
  if (!actor_recipe_capture::physical_span(frame.shadowCursor, 8u)) {
    return Status::InvalidShadowCursor;
  }

  for (uint32_t i = 0; i < kSourceCapacity; ++i) {
    const uint32_t moby = core->mem_r32(kSourceList + i * 4u);
    if (moby == 0u) {
      return Status::Ready;
    }
    if (!actor_recipe_capture::physical_span(moby, 0x58u)) {
      frame = {};
      return Status::InvalidSourceList;
    }
    frame.visitedMobys.push_back(moby);
    ++frame.census.scanned;
    actor_recipe_capture::SourceRecord source{};
    if (!actor_scene::build_source_record(core, moby, source, frame.census)) {
      ++frame.census.culled;
      // 0x800208FC appends a shadow after horizontal culling but before its
      // final vertical cull. A fully rejected source does not reveal which
      // side of that boundary it reached, so do not silently lose state.
      if ((int32_t)core->mem_r32(moby + 0x1cu) < 0) {
        frame = {};
        return Status::CulledShadowSideEffectUnowned;
      }
      continue;
    }
    if (frame.records.size() == actor_recipe_capture::kDurableRecords) {
      frame = {};
      return Status::RecordCapacityExceeded;
    }
    Record record{.moby = moby, .lightingControl = core->mem_r32(moby + 0x4cu)};
    if (!actor_recipe_capture::capture_secondary_source(core, source, record.actor)) {
      frame = {};
      return Status::RecordCaptureRefused;
    }
    frame.records.push_back(std::move(record));
    ++frame.census.queued;

    if ((int32_t)core->mem_r32(moby + 0x1cu) < 0 && source.tz < -0x1200) {
      const uint32_t texture =
          source.descriptor + 0x2au + (uint32_t)core->mem_r8(moby + 0x3eu) * 8u;
      if (!actor_recipe_capture::physical_span(texture & ~3u, 4u) ||
          !actor_recipe_capture::physical_span(
              frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
        frame = {};
        return Status::InvalidShadowCursor;
      }
      frame.shadows.push_back({.moby = moby, .modelByte = core->mem_r8(texture)});
    }
  }
  frame = {};
  return Status::UnterminatedSourceList;
}

void commit(Core *core, const Frame &frame) {
  for (uint32_t moby : frame.visitedMobys) {
    core->mem_w8(moby + 0x51u, 0u);
  }
  for (const Record &record : frame.records) {
    core->mem_w8(record.moby + 0x51u, 1u);
  }
  for (uint32_t i = 0; i < frame.shadows.size(); ++i) {
    const uint32_t out = frame.shadowCursor + i * 8u;
    core->mem_w32(out, frame.shadows[i].moby);
    core->mem_w32(out + 4u, frame.shadows[i].modelByte);
  }
  core->mem_w32(kShadowCursor, frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
}

const char *status_name(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidSourceList:
    return "invalid source list";
  case Status::UnterminatedSourceList:
    return "unterminated source list";
  case Status::RecordCapacityExceeded:
    return "record capacity exceeded";
  case Status::RecordCaptureRefused:
    return "record capture refused";
  case Status::CulledShadowSideEffectUnowned:
    return "culled shadow side effect unowned";
  case Status::InvalidShadowCursor:
    return "invalid shadow cursor";
  }
  return "unknown";
}

} // namespace spyro::secondary_actor_scene
