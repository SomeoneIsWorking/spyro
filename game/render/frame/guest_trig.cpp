#include "guest_trig.h"

#include "core.h"

#include <span>

namespace spyro::guest_trig {
namespace {

constexpr std::uint32_t kSineTable = 0x8006CBF8u;
constexpr std::uint32_t kCosineOffset = 0x80u;

std::int32_t trig(const world_chunk_codec::RamView &ram, std::uint32_t base, std::int32_t angle) {
  const std::uint32_t wrapped = (std::uint32_t)angle & 0xFFFu;
  const std::uint32_t index = wrapped >> 4;
  const std::uint32_t fraction = wrapped & 0xFu;
  const std::int32_t first = (std::int16_t)ram.r16(base + index * 2u);
  if (fraction == 0u) {
    return first;
  }
  const std::int32_t second = (std::int16_t)ram.r16(base + (index + 1u) * 2u);
  return first + (std::int32_t)(fraction * (std::uint32_t)(second - first) >> 4);
}

world_chunk_codec::RamView view(Core *core) {
  return world_chunk_codec::RamView(std::span<const std::uint8_t>(core->ram, sizeof(core->ram)));
}

} // namespace

std::int32_t sine(const world_chunk_codec::RamView &ram, std::int32_t angle) {
  return trig(ram, kSineTable, angle);
}

std::int32_t cosine(const world_chunk_codec::RamView &ram, std::int32_t angle) {
  return trig(ram, kSineTable + kCosineOffset, angle);
}

std::int32_t sine(Core *core, std::int32_t angle) {
  return sine(view(core), angle);
}

std::int32_t cosine(Core *core, std::int32_t angle) {
  return cosine(view(core), angle);
}

} // namespace spyro::guest_trig
