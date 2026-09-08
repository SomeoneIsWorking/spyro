#pragma once

#include <cstdint>
#include <vector>

// Synthetic authored world data shared by capture, sampling and presentation tests.
namespace spyro::testing::world_source_fixture {

constexpr uint32_t kEnvironment = 0x785a8u;
constexpr uint32_t kCamera = 0x76dd0u;
constexpr uint32_t kTable = 0x90000u;
constexpr uint32_t kSector = 0x91000u;
constexpr uint32_t kGroups = 0x92000u;
constexpr uint32_t kGroup = 0x92100u;
inline void w8(std::vector<uint8_t> &ram, uint32_t address, uint8_t value) {
  ram[address] = value;
}

inline void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4u; ++i) {
    w8(ram, address + i, (uint8_t)(value >> (i * 8u)));
  }
}

inline void identity(std::vector<uint8_t> &ram, uint32_t address) {
  w32(ram, address, 0x00001000u);
  w32(ram, address + 4u, 0u);
  w32(ram, address + 8u, 0x00001000u);
  w32(ram, address + 12u, 0u);
  w32(ram, address + 16u, 0x00001000u);
}

inline std::vector<uint8_t> fixture() {
  std::vector<uint8_t> bytes(0x200000u);
  identity(bytes, kCamera);
  identity(bytes, kCamera + 0x14u);
  w32(bytes, kEnvironment, kTable);
  w32(bytes, kEnvironment + 4u, 1u);
  w32(bytes, kEnvironment + 8u, kGroups);
  w32(bytes, kEnvironment + 0x24u, 16000u);
  w32(bytes, kTable, kSector);
  w32(bytes, kSector + 0x18u, 0xffffffffu);
  return bytes;
}

// Invert the production sector-center/camera convention under identity matrices.
inline void view(std::vector<uint8_t> &bytes, int x, int y, int z, unsigned radius) {
  w32(bytes, kSector, 0u);
  w32(bytes, kSector + 4u, radius);
  w32(bytes, kCamera + 0x28u, (uint32_t)(-z * 16));
  w32(bytes, kCamera + 0x2cu, (uint32_t)(x * 16));
  w32(bytes, kCamera + 0x30u, (uint32_t)(y * 16));
}

inline std::vector<uint8_t> lowGeometryFixture() {
  auto bytes = fixture();
  view(bytes, 0, 0, 1000, 0);
  w32(bytes, kSector + 4u, 0x4000u); // LQ only; the unused HQ layout is deliberately invalid.
  w32(bytes, kEnvironment + 0x28u, 65536u);
  w32(bytes, kSector + 0x10u, 0x00010404u);
  constexpr uint32_t vertices = kSector + 0x1cu;
  for (uint32_t i = 0; i < 4; ++i) {
    w32(bytes, vertices + i * 4u, ((i & 1u) ? 64u << 10 : 0u) | ((i & 2u) ? 64u : 0u));
    w32(bytes, vertices + 16u + i * 4u, 0x00123456u + i);
  }
  constexpr uint32_t face = vertices + 32u;
  const uint32_t indices = (1u << 20) | (2u << 14) | (3u << 8);
  w32(bytes, face, indices | 0x80u);
  w32(bytes, face + 4u, indices | 7u);
  w32(bytes, kGroups, kGroup);
  w8(bytes, kGroup, 0u);
  w8(bytes, kGroup + 1u, 0u);
  w8(bytes, kGroup + 2u, 0xffu);
  constexpr uint32_t material = 0x95000u;
  w32(bytes, kEnvironment + 0x20u, 1u);
  w32(bytes, kEnvironment + 0x18u, material);
  w32(bytes, kEnvironment + 0x1cu, material + 16u);
  w32(bytes, material, 0xaabbccddu);

  return bytes;
}

} // namespace spyro::testing::world_source_fixture
