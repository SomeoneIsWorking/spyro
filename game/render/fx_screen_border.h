#pragma once

#include "screen_border_recipe.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::screen_border {

// Guest screen-border producer 0x80018F30: steps the bar-height state at 0x800756C0 by
// g_DeltaTime (0x800756CC) under the enabled flag at 0x8007570C, then pushes the two black bars
// into the HUD layer. Runs only when the guest's own gate is true —
// (enabled != 0 || height != 0) — one call per GS_Playing frame, in the authored draw order.
//
// IT IS TWO STEPS, BECAUSE THEY HAPPEN AT DIFFERENT TIMES. `stage` is the guest's own read of its
// two globals plus the write-back of the stepped height (`sw $v0,0x56c0($at)` at 0x80018F80 and
// 0x80018FA4, and the clearing store at 0x80018FF4): that is GUEST STATE, it must happen on the
// logic frame and only there, and it must happen AFTER the composition's gate has read the pre-step
// value. `submit` is the queue emission, which a reconstruction also performs — from a recipe it
// was handed, not from the globals — so an in-between picture cannot advance the guest's bar height
// by a frame.
screen_border_recipe::Recipe stage(Core *core);

// The guest's own gate, evaluated on the PRE-step state: `if (g_ScreenBorderEnabled || D_800756C0)`
// in GamestateDraw's GS_Playing arm. False means the guest would not have called the producer at
// all, so the stage must not run either.
bool armed(Core *core);

bool submit(Core *core, RenderQueue &target, const screen_border_recipe::Recipe &recipe);

} // namespace spyro::screen_border
