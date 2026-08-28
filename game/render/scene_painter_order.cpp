#include "scene_painter_order.h"

namespace spyro::scene_painter_order {
namespace {

constexpr uint32_t kPhaseShift = 29u;
constexpr uint32_t kOrdinalMask = (1u << kPhaseShift) - 1u;

enum class LinkPhase : uint32_t {
  Cyclorama = 0,
  SecondaryActor = 1,
  Actor = 2,
  QueuedWorld = 3,
  World = 4
};

constexpr uint32_t linkOrdinal(LinkPhase phase, uint32_t ordinal) {
  return ((uint32_t)phase << kPhaseShift) | ordinal;
}

} // namespace

PainterReplayOrder world(uint16_t otBin, uint32_t paintGroup, uint32_t paintSuborder) {
  if (paintGroup > kOrdinalMask) {
    return {};
  }
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::World, paintGroup), paintSuborder}};
}

PainterReplayOrder queuedWorld(uint16_t otBin, uint32_t paintGroup) {
  if (paintGroup > kOrdinalMask) {
    return {};
  }
  // 0x80022A2C runs after both actor submitters and links each accepted
  // polygon at the OT head. Later accepted polygons therefore replay first.
  return {kActorWorldTerrainDomain, {otBin, linkOrdinal(LinkPhase::QueuedWorld, paintGroup), 0}};
}

PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // The coalescer appends later records to the existing global chain. The
  // framework sorts link ordinals descending, so invert the source ordinal.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Actor, kOrdinalMask - recordOrdinal), chainOrdinal}};
}

PainterReplayOrder secondaryActor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80020F34 coalesces into the global OT after 0x8001F798, so its
  // records replay after the regular actor chain within the same OT bin.
  return {
      kActorWorldTerrainDomain,
      {otBin, linkOrdinal(LinkPhase::SecondaryActor, kOrdinalMask - recordOrdinal), chainOrdinal}};
}

PainterReplayOrder cyclorama(uint32_t chainOrdinal) {
  return {kActorWorldTerrainDomain, {2047u, linkOrdinal(LinkPhase::Cyclorama, 0u), chainOrdinal}};
}

} // namespace spyro::scene_painter_order
