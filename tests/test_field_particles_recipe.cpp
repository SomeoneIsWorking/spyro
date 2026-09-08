#include "field_particles_recipe.h"

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
    std::cerr << "field_particles_recipe: " << what << '\n';
    std::exit(1);
  }
}

void testTypeZeroDecode() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80075738u, kBase + 0x1FE0u);
  put32(bytes, kBase + 4u, 0x00160000u);
  put32(bytes, kBase + 8u, 0x00020002u);
  put32(bytes, kBase + 0xcu, 0x030affffu);
  put32(bytes, kBase + 0x20u + 4u, 0xfffe0000u);
  put32(bytes, kBase + 0x20u + 8u, 0x00000002u);
  put32(bytes, kBase + 0x20u + 0xcu, 0x00112233u);
  bytes[kBase - 0x80000000u + 0x40u + 1u] = 0xffu;

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

void testCursorIsNotListEnd() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80075738u, kBase + 0x20u);
  bytes[kBase - 0x80000000u + 1u] = 0xfeu;
  put32(bytes, kBase + 0x20u + 4u, 0x00010000u);
  put32(bytes, kBase + 0x20u + 8u, 0x00020002u);
  put32(bytes, kBase + 0x20u + 0xcu, 0x00030201u);
  bytes[kBase - 0x80000000u + 0x40u + 1u] = 0xffu;

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::Ready,
          "cursor-independent status");
  require(recipe.records == 2u, "cursor-independent record count");
  require(recipe.points.size() == 1u, "free hole skipped");
}

void testUnsupportedTypeRefusal() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80075738u, kBase + 0x20u);
  bytes[kBase - 0x80000000u + 1u] = 3u;

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::UnsupportedType,
          "unsupported status");
  require(recipe.points.empty(), "atomic refusal");
}

void testTypeOneDecode() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  bytes[kBase - 0x80000000u + 1u] = 1u;
  put32(bytes, kBase + 4u, 0x02220111u);
  put32(bytes, kBase + 8u, 0x04440333u);
  put32(bytes, kBase + 0xcu, 0x06660555u);
  put32(bytes, kBase + 0x10u, 0x00112233u);
  put32(bytes, kBase + 0x14u, 0x07445566u);
  put32(bytes, kBase + 0x20u, 0xffffffffu);

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::Ready, "type one status");
  require(recipe.records == 1u, "type one record count");
  require(recipe.lines.size() == 1u, "type one count");
  const auto &line = recipe.lines[0];
  require(line.x0 == 0x0111 && line.y0 == 0x0222 && line.z0 == 0x0333, "type one first endpoint");
  require(line.x1 == 0x0444 && line.y1 == 0x0555 && line.z1 == 0x0666, "type one second endpoint");
  require(line.r0 == 0x33u && line.g0 == 0x22u && line.b0 == 0x11u, "type one first colour");
  require(line.r1 == 0x66u && line.g1 == 0x55u && line.b1 == 0x44u, "type one second colour");
  // The second colour word's command byte is unused by LINE_G2 and carries the shared depth bias.
  require(line.depthBias == 0x07u, "type one depth bias");
}

void testTypeTwoDecode() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80076278u + 14u * 4u, kBase + 0x100u);
  put32(bytes, kBase + 0x100u + 4u, 0x4321abcdu);
  put32(bytes, kBase + 0x100u + 8u, 0x12345678u);
  bytes[kBase - 0x80000000u] = 0x0eu;
  bytes[kBase - 0x80000000u + 1u] = 2u;
  put32(bytes, kBase + 4u, 0x4f117bf3u);
  put32(bytes, kBase + 8u, 0xa24406cdu);
  put32(bytes, kBase + 0xcu, 0x2e7e7e7eu);
  put32(bytes, kBase + 0x10u, 0xffff0400u);
  put32(bytes, kBase + 0x20u, 0xffffffffu);

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::Ready, "type two status");
  require(recipe.records == 1u, "type two record count");
  require(recipe.texturedQuads.size() == 1u, "type two count");
  const auto &quad = recipe.texturedQuads[0];
  require(quad.textureClass == 0x0eu, "type two class");
  require(quad.x == 0x7bf3 && quad.y == 0x4f11 && quad.z == 0x06cd, "type two position");
  require(quad.size == 0x44u, "type two size");
  require(quad.angle == 0x184u, "type two angle table index");
  require(quad.depthBias == 4u, "type two depth bias");
  require(quad.colorCommand == 0x2e7e7e7eu, "type two color command");
  require(quad.uvClut == 0x4321abcdu && quad.uvTpage == 0x12345678u, "type two texture words");
}

void testTypeTwoRejectsMissingTexture() {
  std::array<uint8_t, 0x200000> bytes{};
  put32(bytes, 0x80075824u, kBase);
  put32(bytes, 0x80076278u + 14u * 4u, 0x801ffffcu);
  bytes[kBase - 0x80000000u] = 0x0eu;
  bytes[kBase - 0x80000000u + 1u] = 2u;
  put32(bytes, kBase + 0x20u, 0xffffffffu);

  const auto recipe = spyro::field_particles_recipe::derive(
      spyro::world_chunk_codec::RamView(std::span<const uint8_t>(bytes)));
  require(recipe.status == spyro::field_particles_recipe::Status::InvalidPointers,
          "type two missing texture refusal");
  require(recipe.texturedQuads.empty(), "type two atomic missing texture refusal");
}

} // namespace

int main() {
  testTypeZeroDecode();
  testCursorIsNotListEnd();
  testUnsupportedTypeRefusal();
  testTypeOneDecode();
  testTypeTwoDecode();
  testTypeTwoRejectsMissingTexture();
  std::cout << "field_particles_recipe: PASS (type-0/type-1/type-2 decode + atomic refusal)\n";
  return 0;
}
