#include "secondary_actor_scene.h"

#include <utility>

#include "core.h"

#include <cstdint>

namespace spyro::secondary_actor_scene {
namespace {

constexpr uint32_t kSourceList = 0x80071ef4u;
constexpr uint32_t kSourceCapacity = 256u;
constexpr uint32_t kWasDrawn = 0x51u;
constexpr uint32_t kShadowDistance = 0x1cu;

} // namespace

Status prepare(Core *core, int32_t drawWidth, Frame &frame) {
  frame = {};
  frame.shadowCursor = core->mem_r32(moby_shadow_list::kCursor);
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
    actor_scene::Answers answers{};
    const bool drawn =
        actor_scene::build_source_record(core, moby, drawWidth, source, frame.census, answers);
    if (answers.guest.visible) {
      frame.guestVisibleMobys.push_back(moby);
    }
    // 0x800208FC appends a shadow after horizontal culling even if vertical culling fails, with the
    // same 0x1200 limit as 0x8001F158 (0x80020AF8 `addi $t3,$v1,-0x1200; bgez` skips).
    const bool stages = actor_scene::stages_shadow((int32_t)core->mem_r32(moby + kShadowDistance),
                                                   source.tz,
                                                   actor_scene::kShadowStagingDepth);
    if (stages && answers.guest.horizontal) {
      const auto entry = actor_scene::shadow_entry(core, moby, source.descriptor);
      if (!entry || !actor_recipe_capture::physical_span(
                        frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u, 8u)) {
        frame = {};
        return Status::InvalidShadowCursor;
      }
      frame.shadows.push_back({.moby = moby, .modelByte = entry->radius});
    }
    if (stages && answers.drawn.horizontal) {
      const auto entry = actor_scene::shadow_entry(core, moby, source.descriptor);
      if (!entry) {
        frame = {};
        return Status::InvalidShadowCursor;
      }
      frame.drawnShadows.push_back(*entry);
    }
    if (!drawn) {
      ++frame.census.culled;
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
  }
  frame = {};
  return Status::UnterminatedSourceList;
}

void commit(Core *core, const Frame &frame) {
  for (uint32_t moby : frame.visitedMobys) {
    core->mem_w8(moby + kWasDrawn, 0u);
  }
  for (uint32_t moby : frame.guestVisibleMobys) {
    core->mem_w8(moby + kWasDrawn, 1u);
  }
  for (uint32_t i = 0; i < frame.shadows.size(); ++i) {
    const uint32_t out = frame.shadowCursor + i * 8u;
    core->mem_w32(out, frame.shadows[i].moby);
    core->mem_w32(out + 4u, frame.shadows[i].modelByte);
  }
  core->mem_w32(moby_shadow_list::kCursor,
                frame.shadowCursor + (uint32_t)frame.shadows.size() * 8u);
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
