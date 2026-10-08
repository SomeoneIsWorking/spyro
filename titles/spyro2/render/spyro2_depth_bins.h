// spyro2_depth_bins.h — which depth bin of the ordering table each packet of the frame was linked
// into, taken before the guest flattens the bins into one chain.
//
// FUN_8001B2A8 (called at 800146AC, 80014A54, 800159A4, 8004D938) concatenates the table's 8-byte
// (head, first) bins, deepest first, into the single chain DrawOTag walks, so after it the bins are
// gone. The override reads them first and hands each packet its bin through `core.otTables`. Every
// packet in the bins gets one: terrain, mobys and HUD.
#pragma once

#include "guest_render_globals.h"

#include <cstdint>

class Core;

namespace spyro2::depth_bins {

inline constexpr std::uint32_t kFlatten = 0x8001B2A8u;
inline constexpr std::uint16_t kTable = 0;

// Names the table at the base the guest published and assigns every packet in its bins.
void assignBuckets(Core &core, const spyro::guest_render_globals::Globals &globals);

void registerOverrides(Core &core);

} // namespace spyro2::depth_bins
