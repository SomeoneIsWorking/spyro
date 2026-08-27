#include "scene_painter_order.h"

namespace spyro::scene_painter_order {
namespace {

constexpr uint32_t kPhaseShift = 30u;
constexpr uint32_t kOrdinalMask = (1u << kPhaseShift) - 1u;

enum class LinkPhase : uint32_t { Cyclorama = 0, Actor = 1, World = 2 };

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

PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // The coalescer appends later records to the existing global chain. The
  // framework sorts link ordinals descending, so invert the source ordinal.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Actor, kOrdinalMask - recordOrdinal), chainOrdinal}};
}

PainterReplayOrder cyclorama(uint32_t chainOrdinal) {
  return {kActorWorldTerrainDomain, {2047u, linkOrdinal(LinkPhase::Cyclorama, 0u), chainOrdinal}};
}

} // namespace spyro::scene_painter_order
