#include "scene_painter_order.h"

namespace spyro::scene_painter_order {
namespace {

constexpr uint32_t kPhaseShift = 29u;
constexpr uint32_t kOrdinalMask = (1u << kPhaseShift) - 1u;

enum class LinkPhase : uint32_t {
  Cyclorama = 0,
  PairedActor = 1,
  SecondaryActor = 2,
  Actor = 3,
  QueuedWorld = 4,
  World = 5
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

PainterReplayOrder pairedActor(uint16_t otBin, uint32_t faceOrdinal) {
  if (faceOrdinal > kOrdinalMask) {
    return {};
  }
  // ComposeFrameScene emits Spyro after regular and secondary actors. Keep
  // that position in the shared authored replay domain so FIELD can combine
  // the model with the other world producers in one queue.
  return {kActorWorldTerrainDomain, {otBin, linkOrdinal(LinkPhase::PairedActor, 0u), faceOrdinal}};
}

PainterReplayOrder cyclorama(uint32_t chainOrdinal) {
  return {kActorWorldTerrainDomain, {2047u, linkOrdinal(LinkPhase::Cyclorama, 0u), chainOrdinal}};
}

PainterReplayOrder cycloramaPortal(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal) {
  if (portalOrdinal >= kOrdinalMask || faceOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80050BD0 submits each portal renderer before the final 0x8004EBA8 sky call. The queue's
  // cyclorama phase replays higher link ordinals first, so reserve the descending link range for
  // portal calls and keep face order in the chain suborder.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Cyclorama, kOrdinalMask - portalOrdinal), faceOrdinal}};
}

PainterReplayOrder cycloramaMask(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal) {
  if (portalOrdinal >= kOrdinalMask - 1u || faceOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x8004FEA0 is called immediately before each portal mesh family. The
  // mask must replay first at a tied OT bin, so reserve the link immediately
  // ahead of that portal's mesh link (the comparator walks links descending).
  return {
      kActorWorldTerrainDomain,
      {otBin, linkOrdinal(LinkPhase::Cyclorama, kOrdinalMask - portalOrdinal + 1u), faceOrdinal}};
}

} // namespace spyro::scene_painter_order
