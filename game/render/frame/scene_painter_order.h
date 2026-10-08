#pragma once

#include "painter_object_layer.h"

#include <cstdint>

namespace spyro::scene_painter_order {

// Cutscene handler 0x8001E9C8 builds one OT in this exact producer sequence: actor,
// RenderWorldChunks, cyclorama. FIELD adds secondary actors and the paired Spyro model.
constexpr PainterReplayDomainId kActorWorldTerrainDomain = 0x8001e9c8u;

PainterReplayOrder world(uint16_t otBin, uint32_t paintGroup, uint32_t paintSuborder);
// 0x80022A2C's private within-moby ordering table: `s2 = s1 + 0x900` at r_moby.s 0x8002329C, of
// eight-byte entries (0x80023418 indexes it with `sll v0,3`).
inline constexpr uint16_t kQueuedWorldSubBins = 0x900u / 8u;

// `g_WorldOT` (external/spyro-1 game.sbss.s:333 = 0x80075820) holds this many eight-byte entries,
// the same count the actor scene oracle walks.
inline constexpr uint16_t kWorldOtBins = 0x800u;

// 0x80022A2C does not link its faces into the world ordering table directly: it orders faces
// within one moby in a private sub-table at D_8006FCF4, then chains that table into `g_WorldOT`
// at one bin carried in the GTE's DQB register. `worldBin` places the moby, `subBin` its faces.
PainterReplayOrder queuedWorld(uint16_t worldBin, uint32_t paintGroup, uint16_t subBin);
PainterReplayOrder actor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder secondaryActor(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder pairedActor(uint16_t otBin, uint32_t faceOrdinal);
PainterReplayOrder spyroShadow(uint16_t otBin, uint32_t fanOrdinal);
PainterReplayOrder mobyShadow(uint16_t otBin, uint32_t shadowOrdinal, uint32_t fanOrdinal);
PainterReplayOrder flame(uint16_t otBin, uint32_t partOrdinal, uint32_t faceOrdinal);
PainterReplayOrder glow(uint16_t otBin, uint32_t recordOrdinal, uint32_t fanOrdinal);
PainterReplayOrder sparkle(uint16_t otBin, uint32_t recordOrdinal, uint32_t chainOrdinal);
PainterReplayOrder particle(uint16_t otBin, uint32_t scanOrdinal, uint32_t chainOrdinal);
PainterReplayOrder cyclorama(uint32_t chainOrdinal);
PainterReplayOrder cycloramaPortal(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal);
PainterReplayOrder cycloramaMask(uint16_t otBin, uint32_t portalOrdinal, uint32_t faceOrdinal);

} // namespace spyro::scene_painter_order
