#include "world_scene_prepare.h"

#include <cstdlib>
#include <string_view>
#include <vector>

using spyro::world_chunk_codec::RamView;
using spyro::world_scene_prepare::Prepared;

namespace {

constexpr uint32_t kEnvironment = 0x785a8u;
constexpr uint32_t kCamera = 0x76dd0u;
constexpr uint32_t kTable = 0x90000u;
constexpr uint32_t kSector = 0x91000u;
constexpr uint32_t kGroups = 0x92000u;
constexpr uint32_t kGroup = 0x92100u;

void require(bool value) {
  if (!value) {
    std::abort();
  }
}

void w8(std::vector<uint8_t> &ram, uint32_t address, uint8_t value) {
  ram[address] = value;
}

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) {
    w8(ram, address + i, (uint8_t)(value >> (i * 8u)));
  }
}

void identity(std::vector<uint8_t> &ram, uint32_t address) {
  w32(ram, address, 0x00001000u);
  w32(ram, address + 4u, 0u);
  w32(ram, address + 8u, 0x00001000u);
  w32(ram, address + 12u, 0u);
  w32(ram, address + 16u, 0x00001000u);
}

} // namespace

int main() {
  std::vector<uint8_t> bytes(0x200000u);
  identity(bytes, kCamera + 0x14u);
  w32(bytes, kEnvironment, kTable);
  w32(bytes, kEnvironment + 8u, kGroups);
  w32(bytes, kEnvironment + 0x24u, 16000u);

  const char *why = "none";
  Prepared empty{};
  require(spyro::world_scene_prepare::prepare(RamView(bytes), -1, empty, why));
  require(empty.selectedSectors == 0 && empty.low.empty() && empty.high.empty());

  w32(bytes, kEnvironment + 4u, 1u);
  w32(bytes, kTable, kSector);
  w32(bytes, kSector, 1000u << 16);
  w32(bytes, kSector + 4u, 100u);
  w32(bytes, kSector + 0x18u, 0xffffffffu);
  Prepared flat{};
  require(spyro::world_scene_prepare::prepare(RamView(bytes), -1, flat, why));
  require(flat.selectedSectors == 1 && flat.low.size() == 1 && flat.high.size() == 1);
  require(flat.broadVisible[0] == 0xffu);

  w32(bytes, kGroups, kGroup);
  w8(bytes, kGroup, 0u);
  w8(bytes, kGroup + 1u, 0u);
  w8(bytes, kGroup + 2u, 0xffu);
  Prepared grouped{};
  require(spyro::world_scene_prepare::prepare(RamView(bytes), 0, grouped, why));
  require(grouped.selectedSectors == 2 && grouped.low.size() == 2 && grouped.high.size() == 2);

  w32(bytes, kSector + 0x18u, 0xffffff00u);
  Prepared animated{};
  require(!spyro::world_scene_prepare::prepare(RamView(bytes), -1, animated, why));
  require(why == std::string_view("active_animation"));
  return 0;
}
