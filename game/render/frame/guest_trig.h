#pragma once

// guest_trig — the title's Sin and Cos, the pair every guest routine calls with a 12-bit angle.
//
// The table at 0x8006CBF8 has 256 entries for a full turn, with cosine one quarter turn (0x80
// bytes) along, so a 12-bit angle indexes it by its top eight bits and interpolates linearly across
// the remaining four. Callers that already hold a byte index want actor_transform_math's
// sineCosine instead; this is for the ones holding a real angle, where dropping the fraction
// visibly steps the motion.

#include "world_chunk_codec.h"

#include <cstdint>

class Core;

namespace spyro::guest_trig {

std::int32_t sine(const world_chunk_codec::RamView &ram, std::int32_t angle);
std::int32_t cosine(const world_chunk_codec::RamView &ram, std::int32_t angle);
std::int32_t sine(Core *core, std::int32_t angle);
std::int32_t cosine(Core *core, std::int32_t angle);

} // namespace spyro::guest_trig
