#include "world_source_animation.h"

#include "world_scene_prepare.h"

#include <array>
#include <vector>

namespace spyro::world_source_animation {
namespace {

// Channel 3 interleaves two colour streams from the high chunk's layout word; every write
// alternates between them in the order the guest emits it.
bool applyHighColours(world_source::Sector &sector,
                      uint32_t address,
                      uint32_t channel,
                      const world_animation::Plan &plan,
                      const char *&why) {
  const uint32_t layout = sector.high.layout;
  const uint32_t first =
      sector.high.address + 0x1cu + ((layout >> 22) & 0x3fcu) + ((layout << 2) & 0x3fcu);
  const uint32_t second = first + ((layout >> 6) & 0x3fcu);
  size_t wordIndex = 0;
  for (const auto &write : plan.writes) {
    if (write.width == 1u) {
      if (write.address != address + 24u + channel) {
        why = "animation_endpoint_stamp";
        return false;
      }
      continue;
    }
    const uint32_t streamBase = (wordIndex++ & 1u) == 0u ? first : second;
    auto &stream = (wordIndex & 1u) == 1u ? sector.high.farColors : sector.high.nearColors;
    if (write.address < streamBase || write.address >= streamBase + stream.size() * 4u ||
        ((write.address - streamBase) & 3u)) {
      why = "animation_endpoint_destination";
      return false;
    }
    const size_t index = (write.address - streamBase) / 4u;
    if (index >= stream.size()) {
      why = "animation_endpoint_index";
      return false;
    }
    stream[index] = write.value;
  }
  return true;
}

bool applyWords(std::vector<uint32_t> &values,
                uint32_t base,
                uint32_t address,
                uint32_t channel,
                const world_animation::Plan &plan,
                const char *&why) {
  for (const auto &write : plan.writes) {
    if (write.width == 1u) {
      if (write.address != address + 24u + channel) {
        why = "animation_endpoint_stamp";
        return false;
      }
      continue;
    }
    if (write.address < base || ((write.address - base) & 3u)) {
      why = "animation_endpoint_destination";
      return false;
    }
    const size_t index = (write.address - base) / 4u;
    if (index >= values.size()) {
      why = "animation_endpoint_index";
      return false;
    }
    values[index] = write.value;
  }
  return true;
}

bool applyToChunk(world_source::Sector &sector,
                  uint32_t address,
                  uint32_t channel,
                  const world_animation::Plan &plan,
                  const char *&why) {
  const bool low = channel < 2u;
  if ((low && sector.lowStatus != world_chunk_codec::Status::Ok) ||
      (!low && sector.highStatus != world_chunk_codec::Status::Ok)) {
    why = "animation_endpoint_chunk";
    return false;
  }
  switch (channel) {
  case 0u:
    return applyWords(sector.low.vertices, sector.low.address + 0x1cu, address, channel, plan, why);
  case 1u:
    return applyWords(sector.low.colors,
                      sector.low.address + 0x1cu + (uint32_t)sector.low.vertices.size() * 4u,
                      address,
                      channel,
                      plan,
                      why);
  case 2u:
    return applyWords(sector.high.vertices,
                      sector.high.address + 0x1cu + ((sector.high.layout >> 22) & 0x3fcu),
                      address,
                      channel,
                      plan,
                      why);
  default:
    return applyHighColours(sector, address, channel, plan, why);
  }
}

} // namespace

bool applyChannel(world_source::Source &source,
                  uint8_t index,
                  uint32_t address,
                  uint32_t channel,
                  const world_animation::Plan &plan,
                  const char *&why) {
  auto &header = source.selection.sectors[index];
  if (!header || header->address != address) {
    why = "animation_endpoint_sector";
    return false;
  }
  auto &sector = source.sectors[index];
  if (!sector) {
    why = "animation_endpoint_chunk";
    return false;
  }
  if (channel >= 4u || plan.channels != 1u) {
    why = "animation_endpoint_channels";
    return false;
  }
  size_t stamps = 0;
  for (const auto &write : plan.writes) {
    if (write.width == 1u) {
      ++stamps;
    } else if (write.width != 4u) {
      why = "animation_endpoint_width";
      return false;
    }
  }
  if (stamps != 1u) {
    why = "animation_endpoint_stamp";
    return false;
  }
  if (!applyToChunk(*sector, address, channel, plan, why)) {
    return false;
  }
  // The channel's stamp write is what makes the guest's next walk skip it; the Source records the
  // same consumption in its own header copy, and nowhere else.
  header->animation |= 0xffu << (channel * 8u);
  return true;
}

bool animateDrawnOnly(const world_chunk_codec::RamView &ram,
                      world_source::Source &source,
                      const char *&why) {
  if (!source.selection.valid) {
    return true;
  }
  world_scene_prepare::Prepared prepared{};
  if (!world_scene_prepare::prepare(source.selection, source.clipRight, prepared, why, true)) {
    return false;
  }
  std::array<bool, 256> visited{};
  for (const auto &sector : prepared.animations) {
    if (sector.guest || visited[sector.index]) {
      continue;
    }
    visited[sector.index] = true;
    for (uint32_t channel = 0; channel < 4u; ++channel) {
      const uint8_t keyframe = (uint8_t)(sector.active >> (channel * 8u));
      if (keyframe >= 0x80u) {
        continue;
      }
      const uint32_t single =
          (0xffffffffu & ~(0xffu << (channel * 8u))) | ((uint32_t)keyframe << (channel * 8u));
      world_animation::Plan plan{};
      if (!world_animation::appendSector(ram, sector.address, single, plan, why) ||
          !applyChannel(source, sector.index, sector.address, channel, plan, why)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace spyro::world_source_animation
