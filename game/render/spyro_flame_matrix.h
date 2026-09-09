#pragma once

#include <array>
#include <cstdint>

class Core;

// Cross-producer state publication, not geometry: retail's Spyro renderer 0x80023AC4 reads its live
// GTE rotation matrix back with cfc2 at 0x8002401C and writes those five packed words into
// g_SpyroFlame+0xB8 at 0x80024110, gated on the enable byte at g_SpyroFlame+0x9A. The flame
// renderer 0x80058D64 then uses them as the flame's orientation and has no other source for it, so
// a native Spyro producer that does not carry this over leaves the flame collapsed onto its origin.
//
// The matrix published is the camera rotation composed with g_Spyro's own rotation (+0x0C) and then
// its first child rotation (+0x10) — the port's layer 1 matrix — because retail publishes between
// those two composition steps and the layer 2 rotation restores the layer 0 matrix first.
//
// Returns whether the enable byte was set and the words were written.
bool spyro_flame_matrix_publish(Core *core, const std::array<uint32_t, 5> &matrix);
