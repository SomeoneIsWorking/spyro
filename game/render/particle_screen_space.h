// Horizontal screen-space policy for the two field-particle producers.
//
// WHY THIS EXISTS. Both particle producers write a visibility byte back into the guest's own
// particle record, standing in for the guest routine they replace. Both computed that byte from the
// WIDENED horizontal window, so turning widescreen on changed guest memory: MEASURED 2026-09-19 on
// Spyro 1, the same gameplay frame emitted 8 particle primitives at 4:3 and 20 at 16:9, and a solid
// (248,96,0) quad appeared 261 pixels deep INSIDE the shared field of view where the 4:3 frame drew
// grass. Widescreen is a presentation change; it must not decide what the guest believes.
//
// So the two questions are separated here, once, for both producers: what the guest's own
// projection would have called on-screen, and what this port draws. Only the horizontal window
// differs between them -- the projection is otherwise identical -- so the mapping is exact rather
// than a tuned offset.
#pragma once

#include "native_projection.h"

struct Core;

namespace spyro::particle_screen_space {

// The horizontal window the guest's own routine tests against. Retail renders 512 px wide and its
// particle visibility test is written against that window, whatever this port presents into.
inline constexpr int kGuestClipRight = 512;

// The horizontal window this port DRAWS into: the wide engine's width when it is on, else the
// guest's own. Was duplicated in both particle submitters.
int drawClipRight(Core *core);

// Projection parameters for a particle, for a given draw window. `ofx` is the only field the wide
// engine changes; `ofy` and `h` come from the frame's own projection either way.
psxport::native_projection::ProjectionParams projection(Core *core, int drawClipRight);

// Whether the guest's own projection would have called this x horizontally on-screen, given the x
// THIS port projected with `drawClipRight`. The two projections differ only by their horizontal
// offset, so subtracting that difference recovers the guest's x exactly.
bool guestOnScreenX(Core *core, int drawClipRight, int32_t drawnX);

// Whether this port will draw at this x -- the widened window.
bool drawnOnScreenX(int drawClipRight, int32_t drawnX);

} // namespace spyro::particle_screen_space
