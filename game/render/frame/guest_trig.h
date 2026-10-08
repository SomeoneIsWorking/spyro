#pragma once

// Title Sin and Cos with 12-bit angle indexing: a 256-entry table at 0x8006CBF8 with a 0x80-byte
// quarter turn, the top bits indexing it and the low bits interpolating.

#include "world_chunk_codec.h"

#include <cstdint>

class Core;

namespace spyro::guest_trig {

std::int32_t sine(const world_chunk_codec::RamView &ram, std::int32_t angle);
std::int32_t cosine(const world_chunk_codec::RamView &ram, std::int32_t angle);
std::int32_t sine(Core *core, std::int32_t angle);
std::int32_t cosine(Core *core, std::int32_t angle);

} // namespace spyro::guest_trig
