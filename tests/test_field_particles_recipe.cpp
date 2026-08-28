#include "field_particles_recipe.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>

namespace {

constexpr uint32_t kBase = 0x80010000u;
constexpr uint32_t kCursor = kBase + 0x40u;

void put32(std::array<uint8_t, 0x200000> &ram, uint32_t address, uint32_t value) {
  std::memcpy(ram.data() + (address - 0x80000000u), &value, sizeof(value));
}

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "field_particles_recipe: " << what << '\n';
    std::exit(1);
  }
}

void testTypeZeroDecode() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80075738u, kCursor);
  put32(bytes, kBase + 4u, 0x00160000u);
  put32(bytes, kBase + 8u, 0x00020002u);
  put32(bytes, kBase + 0xcu, 0x030affffu);
  put32(bytes, kBase + 0x20u + 4u, 0xfffe0000u);
  put32(bytes, kBase + 0x20u + 8u, 0x00000002u);
  put32(bytes, kBase + 0x20u + 0xcu, 0x00112233u);

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::Ready, "type zero status");
  require(recipe.records == 2u, "record count");
  require(recipe.points.size() == 2u, "point count");
  require(recipe.points[0].x == 0, "first x");
  require(recipe.points[0].y == 22, "first y");
  require(recipe.points[0].z == 2, "first z");
  require(recipe.points[0].depthBias == 2u, "first depth bias");
  require(recipe.points[0].r == 255u, "first red");
  require(recipe.points[0].g == 255u, "first green");
  require(recipe.points[0].b == 10u, "first blue");
  require(recipe.points[1].x == 0, "second x");
  require(recipe.points[1].y == -2, "second y");
  require(recipe.points[1].r == 0x33u, "second red");
  require(recipe.points[1].g == 0x22u, "second green");
  require(recipe.points[1].b == 0x11u, "second blue");
}

void testUnsupportedTypeRefusal() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80075738u, kBase + 0x20u);
  bytes[kBase - 0x80000000u + 1u] = 2u;

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::UnsupportedType,
          "unsupported status");
  require(recipe.points.empty(), "atomic refusal");
}

} // namespace

int main() {
  testTypeZeroDecode();
  testUnsupportedTypeRefusal();
  std::cout << "field_particles_recipe: PASS (type-0 decode + atomic unsupported-type refusal)\n";
  return 0;
}
