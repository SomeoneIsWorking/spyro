#include "field_tracers_recipe.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>

namespace {

constexpr uint32_t kBase = 0x80010000u;

void put32(std::array<uint8_t, 0x200000> &ram, uint32_t address, uint32_t value) {
  std::memcpy(ram.data() + (address - 0x80000000u), &value, sizeof(value));
}

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "field_tracers_recipe: " << what << '\n';
    std::exit(1);
  }
}

void testDecode() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075684u, 1u);
  put32(bytes, 0x800772c8u, 2u);
  put32(bytes, 0x80078658u, kBase);
  put32(bytes, kBase, 10u);
  put32(bytes, kBase + 4u, 20u);
  put32(bytes, kBase + 8u, 30u);
  put32(bytes, kBase + 24u, 40u);
  put32(bytes, kBase + 0x1cu, 50u);
  put32(bytes, kBase + 0x20u, 60u);
  put32(bytes, kBase + 0x24u, 70u);
  put32(bytes, kBase + 0x34u, 80u);

  const auto recipe = spyro::field_tracers_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_tracers_recipe::Status::Ready, "ready status");
  require(recipe.tracerCount == 1u, "tracer count");
  require(recipe.chains.size() == 1u && recipe.chains[0].points.size() == 2u, "point shape");
  require(recipe.chains[0].points[1].z == 70, "second z");
  require(recipe.chains[0].points[1].age == 80, "second age");
}

void testInvalidPointerRefusal() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075684u, 1u);
  put32(bytes, 0x800772c8u, 2u);
  put32(bytes, 0x80078658u, 0x80200000u);
  const auto recipe = spyro::field_tracers_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_tracers_recipe::Status::InvalidPointers, "invalid status");
  require(recipe.chains.empty(), "atomic refusal");
}

} // namespace

int main() {
  testDecode();
  testInvalidPointerRefusal();
  std::cout << "field_tracers_recipe: PASS (point decode + atomic pointer refusal)\n";
  return 0;
}
