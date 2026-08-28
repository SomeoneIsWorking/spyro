#pragma once

#include <cstdint>

class Core;

// Guest screen-border producer 0x80018F30: steps the bar-height state at 0x800756C0 by
// g_DeltaTime (0x800756CC) under the enabled flag at 0x8007570C, then pushes the two black bars
// into the HUD layer. Runs only when the guest's own gate is true —
// (enabled != 0 || height != 0) — one call per GS_Playing frame, in the authored draw order.
bool spyro_screen_border_submit(Core *core,
                                int32_t drawOffsetX,
                                int32_t drawOffsetY,
                                int32_t renderWidth);
