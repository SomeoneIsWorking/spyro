#pragma once

#include "painter_object_layer.h"

#include <cstdint>

namespace spyro::scene_painter_order {

// 0x8001E9C8 builds one stage-13 OT in this exact producer sequence: actor,
// RenderWorldChunks, cyclorama. World uses head insertion and therefore
// replays before the actor chain; actor's coalescer appends records in source
// order; cyclorama explicitly patches the old tail and replays last.
constexpr PainterReplayDomainId kStage13Domain = 0x8001e9c8u;

PainterReplayOrder world(uint16_t otBin, uint32_t paintGroup, uint32_t paintSuborder);
PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder cyclorama(uint32_t chainOrdinal);

} // namespace spyro::scene_painter_order
