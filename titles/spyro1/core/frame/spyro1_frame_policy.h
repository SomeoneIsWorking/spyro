#pragma once

#include "field_owner.h"

#include <cstdint>

namespace spyro1 {

// The retained Spyro frame tail spends at least two display fields per drawn logic iteration
// (GamestateDraw's `< 2` wait). Native rendering supplies those fields through frame_commit; a
// draw-less iteration (g_StateSwitch pending) has no quota, exactly as in retail main.c. The
// counter that tracks them is the shared one every title in this lineage uses.
inline constexpr std::uint32_t kFieldsPerLogicFrame = 2;

using FieldCadence = spyro::FieldCadence;

} // namespace spyro1
