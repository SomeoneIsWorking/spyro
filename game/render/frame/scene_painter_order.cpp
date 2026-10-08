#include "scene_painter_order.h"

namespace spyro::scene_painter_order {
namespace {

constexpr uint32_t kPhaseShift = 28u;
constexpr uint32_t kOrdinalMask = (1u << kPhaseShift) - 1u;

// Ascending in the order the guest links each producer's packets, which the framework replays
// descending. Twelve phases no longer fit three bits, so the field is four bits wide; the ordinal
// range that leaves is still four orders of magnitude above any producer's record count.
enum class LinkPhase : uint32_t {
  Particle = 0,
  Cyclorama = 1,
  Sparkle = 2,
  Glow = 3,
  Flame = 4,
  SpyroShadow = 5,
  MobyShadow = 6,
  PairedActor = 7,
  SecondaryActor = 8,
  Actor = 9,
  QueuedWorld = 10,
  World = 11
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

PainterReplayOrder queuedWorld(uint16_t worldBin, uint32_t paintGroup, uint16_t subBin) {
  if (paintGroup > kOrdinalMask || subBin >= kQueuedWorldSubBins) {
    return {};
  }
  // 0x80022A2C runs after both actor submitters and links each accepted polygon at the OT head, so
  // later accepted polygons replay first.
  //
  // The sub-bin is inverted: retail walks its sub-table from the highest entry downwards
  // (0x80023990), so a larger sub-bin is a farther face drawn first, while painterReplayBefore
  // orders smaller first and the raw sub-bin would replay each moby's faces inside-out.
  return {kActorWorldTerrainDomain,
          {worldBin,
           linkOrdinal(LinkPhase::QueuedWorld, paintGroup),
           (uint16_t)(kQueuedWorldSubBins - 1u - subBin)}};
}

PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // The coalescer appends later records to the existing global chain, and the framework sorts link
  // ordinals descending, so the source ordinal is inverted.
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
  // ComposeFrameScene emits Spyro after regular and secondary actors; FIELD combines that with the
  // other world producers in one queue.
  return {kActorWorldTerrainDomain, {otBin, linkOrdinal(LinkPhase::PairedActor, 0u), faceOrdinal}};
}

PainterReplayOrder spyroShadow(uint16_t otBin, uint32_t fanOrdinal) {
  if (fanOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80059A48 is called after Spyro's model and links its fan in ascending packet order, so the
  // fan ordinal is the chain suborder rather than an inverted allocation index.
  return {kActorWorldTerrainDomain, {otBin, linkOrdinal(LinkPhase::SpyroShadow, 0u), fanOrdinal}};
}

PainterReplayOrder mobyShadow(uint16_t otBin, uint32_t shadowOrdinal, uint32_t fanOrdinal) {
  if (shadowOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80059F8C walks its shadow list front to back and links each fan in ascending packet order, so
  // later shadows sit deeper in the chain and the source ordinal is inverted like the actor chains.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::MobyShadow, kOrdinalMask - shadowOrdinal), fanOrdinal}};
}

PainterReplayOrder flame(uint16_t otBin, uint32_t partOrdinal, uint32_t faceOrdinal) {
  if (partOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80058D64 walks its eight flame parts from the last to the first and appends every packet, so
  // an earlier-walked part sits deeper in the chain: the ordinal is already inverted.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Flame, partOrdinal), faceOrdinal}};
}

PainterReplayOrder glow(uint16_t otBin, uint32_t recordOrdinal, uint32_t fanOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x800580F4 walks its sixteen records in ascending order and appends every packet, so an
  // earlier record sits deeper in the chain than a later one.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Glow, kOrdinalMask - recordOrdinal), fanOrdinal}};
}

PainterReplayOrder sparkle(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal) {
  if (recordOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x800584C4 walks its eight records in ascending order and appends both of each sparkle's line
  // packets, so an earlier record sits deeper in the chain. The two lines of one sparkle keep their
  // own linked order through the chain suborder.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Sparkle, kOrdinalMask - recordOrdinal), chainOrdinal}};
}

PainterReplayOrder particle(uint16_t otBin, uint32_t scanOrdinal, uint32_t chainOrdinal) {
  if (scanOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x800573C8 walks the emit list once in slot order and appends every arm's packet from that one
  // scan, so the ordinal is the record's position in that scan: the three arms interleave, and
  // sorting each separately would reorder overlapping particles against the guest.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Particle, kOrdinalMask - scanOrdinal), chainOrdinal}};
}

PainterReplayOrder cyclorama(uint32_t chainOrdinal) {
  return {kActorWorldTerrainDomain, {2047u, linkOrdinal(LinkPhase::Cyclorama, 0u), chainOrdinal}};
}

PainterReplayOrder cycloramaPortal(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal) {
  if (portalOrdinal >= kOrdinalMask || faceOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x80050BD0 submits each portal renderer before the final 0x8004EBA8 sky call, and the queue's
  // cyclorama phase replays higher link ordinals first, so the descending range is reserved here.
  return {kActorWorldTerrainDomain,
          {otBin, linkOrdinal(LinkPhase::Cyclorama, kOrdinalMask - portalOrdinal), faceOrdinal}};
}

PainterReplayOrder cycloramaMask(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal) {
  if (portalOrdinal >= kOrdinalMask - 1u || faceOrdinal > kOrdinalMask) {
    return {};
  }
  // 0x8004FEA0 is called immediately before each portal mesh family, so the mask must replay first
  // at a tied OT bin: reserve the link immediately ahead of that portal's mesh link.
  return {
      kActorWorldTerrainDomain,
      {otBin, linkOrdinal(LinkPhase::Cyclorama, kOrdinalMask - portalOrdinal + 1u), faceOrdinal}};
}

} // namespace spyro::scene_painter_order
