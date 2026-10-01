// spyro2_moby_gte.h — what Spyro 2's moby visibility walk (0x80043858; see
// spyro2_moby_visibility.h) keeps in GTE registers, for its two owners
// (spyro2_moby_visibility, spyro2_moby_rotation).
//
// The walk is hand-written assembly that runs out of general registers, so it parks state in GTE
// registers whose hardware meaning it never uses: VXY1/VZ1 carry two values to the moby drawer
// that runs next, the light-matrix words CR8..CR11 hold list cursors, and TRX..TRZ hold the
// current moby's view-space centre. Each is named for what this routine keeps in it; the
// hardware names are spyro2_gte.h's.
#pragma once

#include "spyro2_gte.h"

#include <cstdint>

namespace spyro2::moby_gte {

using namespace spyro2::gte;

// L11L12: next free deferred-moby slot.
inline constexpr std::uint32_t kDeferredCursor = kLight0;
// L13L21: end of the deferred-moby buffer.
inline constexpr std::uint32_t kDeferredEnd = kLight1;
// L22L23: the close-list cursor, while LO is busy.
inline constexpr std::uint32_t kParkedCursor = kLight2;
// L31L32: the render-list end, while HI is busy.
inline constexpr std::uint32_t kParkedListEnd = kLight3;

} // namespace spyro2::moby_gte
