#pragma once

#include "painter_object_layer.h"

#include <cstdint>

namespace spyro::scene_painter_order {

// Cutscene handler 0x8001E9C8 builds one OT in this exact producer sequence: actor,
// RenderWorldChunks, cyclorama. FIELD additionally composes secondary actors and the paired Spyro
// model in the same authored domain. The selected stage-13 overlay independently uses the base splice.
// World uses head insertion and therefore
// replays before the actor chain; actor's coalescer appends records in source
// order; cyclorama explicitly patches the old tail and replays last.
constexpr PainterReplayDomainId kActorWorldTerrainDomain = 0x8001e9c8u;

PainterReplayOrder world(uint16_t otBin, uint32_t paintGroup, uint32_t paintSuborder);
PainterReplayOrder queuedWorld(uint16_t otBin, uint32_t paintGroup);
PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder secondaryActor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder pairedActor(uint16_t otBin, uint32_t faceOrdinal);
PainterReplayOrder cyclorama(uint32_t chainOrdinal);
PainterReplayOrder cycloramaPortal(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal);
PainterReplayOrder cycloramaMask(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal);

} // namespace spyro::scene_painter_order
