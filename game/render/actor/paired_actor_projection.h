#pragma once

#include "paired_actor_decode.h"

#include <array>
#include <cstdint>

// One vertex of guest renderer 0x80023AC4 through the coprocessor, in the exact fields the port's
// primitive resolver and its temporal rebuild both consume.
//
// This owner exists because THREE callers need the same arithmetic and it is not incidental to
// any of them: the live producer projects a decoded pose with it, the temporal owner re-projects
// each captured endpoint from its stored pose, and the endpoint compatibility census projects both
// endpoints again at the midpoint. Three spellings of one projection would let two of them drift.
namespace spyro::paired_actor_projection {

// Project one RTPS vertex through the coprocessor matrix `cr` (the 27 control words: 0..4 the
// rotation, 5..7 the translation, 24..26 the projection leaves). `d0` and `d1` are the packed DR0
// and DR1 inputs, already unpacked by the pose owner.
[[nodiscard]] spyro::paired_actor::ProjectedVertex
projectRtps(std::uint32_t d0, std::uint32_t d1, const std::array<std::uint32_t, 27> &cr);

// The guest's own rounding of a screen coordinate: halves go AWAY from zero, not to even.
[[nodiscard]] int roundScreen(float value);

} // namespace spyro::paired_actor_projection
