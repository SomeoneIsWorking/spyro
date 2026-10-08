// native_collision_shade.h — Spyro 1 (SCUS_942.28) collision surface shade word, owned natively.
//
// The body replaced here is the collision raycast's shade writer: it measures the surface normal
// the raycast left in g_CollisionNormal and packs that orientation into the caller's own latch
// word. Held by the override differential (psxport docs/issues/0138):
// the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerCollisionShadeOverrides(Core &core);

} // namespace spyro1::native
