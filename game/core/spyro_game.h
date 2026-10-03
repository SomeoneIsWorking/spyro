#pragma once

#include <array>
#include <cstdint>

class Core;

namespace spyro {

// Image-scoped native owners. Each installer binds the verified handwritten body to the image that
// currently owns its guest address, so a title declares WHICH image it is and never an address.
void registerNativeRand(Core &core);
void registerNativeLeaves(Core &core);
void registerNativeVec(Core &core);
void registerNativeGte(Core &core);
void registerNativeAngle(Core &core);
void registerNativeUtil(Core &core);
// The title's cooperative CD loader leaves and their completion delivery.
void registerCdQueue(Core &core);

} // namespace spyro
