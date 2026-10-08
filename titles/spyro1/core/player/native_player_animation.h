// native_player_animation.h — Spyro 1 (SCUS_942.28) Spyro's body, head and tail animation steppers,
// owned natively.
//
// Each function here replaces the guest body of the same address in external/spyro-1/src/pete.c and
// is held by the override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerPlayerAnimationOverrides(Core &core);

} // namespace spyro1::native
