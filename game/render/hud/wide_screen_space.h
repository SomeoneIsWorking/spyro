// Horizontal screen-space policy for every producer that projects world geometry.
//
// Both particle producers write a visibility byte back into the guest's own particle record,
// standing in for the guest routine they replace. Computing that byte from the WIDENED horizontal
// window would make turning widescreen on change guest memory: widescreen is a presentation change
// and must not decide what the guest believes.
//
// So the two questions are separated here: what the guest's own projection would have called
// on-screen, and what this port draws. Only the horizontal window differs between them.
#pragma once

#include "native_projection.h"

struct Core;

namespace spyro::wide_screen_space {

// The horizontal centre this frame's geometry is projected about, for either aspect. The number
// belongs to the framework; this is the name Spyro's producers ask for it by.
int32_t horizontalCenter(Core *core);

// The horizontal window the guest's own routine tests against. Retail renders 512 px wide and its
// particle visibility test is written against that window, whatever this port presents into.
inline constexpr int kGuestClipRight = 512;

// The horizontal window this port DRAWS into: the wide engine's width when it is on, else the
// guest's own. The guard belongs here, once, because `gpu_vk_wide_engine` itself dereferences
// `c->game` without checking it.
int drawClipRight(Core *core);

// The right edge (inclusive) of the draw area a producer submits into: the guest's own GP0 area
// edge `guestAreaRight`, pushed out to the last drawn column when the wide engine is on. Never
// narrows it, and leaves it untouched at 4:3.
int drawAreaRight(Core *core, int guestAreaRight);

// Projection parameters for a particle. `ofx` is the only field the wide engine changes; `ofy`
// and `h` come from the frame's own projection either way.
psxport::native_projection::ProjectionParams projection(Core *core);

// How far right of the guest's own projection this port projects: the widened horizontal centre
// minus the guest's. Zero at 4:3. Subtracting it from a drawn x recovers the guest's x exactly.
int32_t horizontalOffsetDelta(Core *core);

// The one horizontal on-screen rule: strictly inside (0, right). The guest's window and the drawn
// window apply the same rule with different right edges.
constexpr bool onScreenX(int32_t x, int32_t right) {
  return x > 0 && x < right;
}

// The x the guest's own projection would have produced for a vertex THIS port projected at
// `drawnX`. An arm that tests a bounding box edge by edge rather than a point needs the recovered x
// itself, not the predicate below.
int32_t guestX(Core *core, int32_t drawnX);

// Whether the guest's own projection would have called this x horizontally on-screen, given the x
// THIS port projected.
bool guestOnScreenX(Core *core, int32_t drawnX);

// Whether this port will draw at this x -- the widened window.
bool drawnOnScreenX(int drawClipRight, int32_t drawnX);

} // namespace spyro::wide_screen_space
