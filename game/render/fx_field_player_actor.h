#pragma once

#include <cstdint>

class Core;
struct SpyroPairedActorFrameState;

// Retail func_80019698 renders Spyro only while g_IsSpyroHidden is zero.
inline bool spyro_field_player_visible(uint32_t isSpyroHidden) {
  return isSpyroHidden == 0u;
}

// FIELD-facing owner for retail func_80023AC4, the normal Spyro model arm.
// The paired actor implementation remains shared with the stage-13 mode-3
// owner; this wrapper supplies the FIELD visibility boundary.
bool spyro_field_player_visible(Core *core);
bool spyro_field_player_submit(Core *core, SpyroPairedActorFrameState &state);
