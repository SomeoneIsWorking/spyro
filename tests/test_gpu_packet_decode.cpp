// Discriminator coverage for the retail packet decoder. The actor-scene oracle compares the retail
// OT chain against the native stream, and a code family this decoder refuses is reported as a
// silent producer: the shaded pass (func_80022A2C) writes flat polygons, so a gouraud-only decoder
// made it look like it drew nothing. Every family in the range is exercised here, plus the refusals
// that must stay refusals.

#include "core.h"
#include "game.h"
#include "gpu_packet_decode.h"
#include "testutil.h"

#include <cstdio>
#include <cstring>
#include <memory>

namespace {

constexpr uint32_t kPacket = 0x80100000u;

std::unique_ptr<Game> emptyGame() {
  auto game = std::make_unique<Game>();
  game->core.mem_w32(kPacket, 0u);
  return game;
}

uint32_t word(uint32_t offset) {
  return kPacket + offset * 4u;
}

void writeTag(Core &core, uint8_t wordCount, uint32_t next = 0u) {
  core.mem_w32(word(0), ((uint32_t)wordCount << 24) | (next & 0x00ffffffu));
}

void writeCommand(Core &core, uint8_t code, uint32_t rgb = 0u) {
  core.mem_w32(word(1), ((uint32_t)code << 24) | (rgb & 0x00ffffffu));
}

void writeXy(Core &core, uint32_t wordIndex, int16_t x, int16_t y) {
  core.mem_w32(word(wordIndex), ((uint32_t)(uint16_t)y << 16) | (uint16_t)x);
}

// The refusal string is printed on failure: a decoder that refuses a packet must say which check
// rejected it, or a red test is indistinguishable from a byte the fixture wrote in the wrong place.
bool decodes(Core &core, uint8_t wordCount, spyro::gpu_packet_decode::Packet &out) {
  const char *refusal = "none";
  if (spyro::gpu_packet_decode::decode(&core, kPacket, 0x0002u, wordCount, out, refusal)) {
    return true;
  }
  std::fprintf(stderr, "    decode refused: %s\n", refusal);
  return false;
}

void test_gouraud_untextured_triangle_keeps_per_vertex_colour() {
  auto game = emptyGame();
  Core &core = game->core;
  writeTag(core, 6);
  writeCommand(core, 0x30u, 0x112233u);
  writeXy(core, 2, 10, 20);
  core.mem_w32(word(3), 0x00445566u);
  writeXy(core, 4, 30, 40);
  core.mem_w32(word(5), 0x00778899u);
  writeXy(core, 6, 50, 60);
  spyro::gpu_packet_decode::Packet packet{};
  CHECK(decodes(core, 6, packet));
  CHECK_EQ(packet.code, 0x30u);
  CHECK_EQ(packet.vertexCount, 3u);
  CHECK(!packet.textured);
  CHECK(!packet.semiTransparent);
  CHECK_EQ(packet.vertices[0].rgb, 0x00112233u);
  CHECK_EQ(packet.vertices[1].rgb, 0x00445566u);
  CHECK_EQ(packet.vertices[2].rgb, 0x00778899u);
  CHECK_EQ(packet.vertices[0].sx, 10);
  CHECK_EQ(packet.vertices[2].sy, 60);
}

void test_flat_untextured_polygon_carries_one_colour() {
  auto game = emptyGame();
  Core &core = game->core;
  writeTag(core, 4);
  // 0x22: flat (bit4 clear), three vertices (bit3 clear), no texture (bit2 clear), semi-transparent
  // (bit1 set). This is the shaded pass's three-vertex polygon code.
  writeCommand(core, 0x22u, 0x000073u);
  writeXy(core, 2, 251, 107);
  writeXy(core, 3, 254, 108);
  writeXy(core, 4, 252, 108);
  spyro::gpu_packet_decode::Packet packet{};
  CHECK(decodes(core, 4, packet));
  CHECK_EQ(packet.code, 0x22u);
  CHECK_EQ(packet.vertexCount, 3u);
  CHECK(!packet.textured);
  CHECK(packet.semiTransparent);
  CHECK_EQ(packet.vertices[0].rgb, 0x00000073u);
  CHECK_EQ(packet.vertices[2].rgb, 0x00000073u);
  CHECK_EQ(packet.vertices[1].sx, 254);
  CHECK_EQ(packet.vertices[2].sy, 108);
  // The blend bits of an untextured semi-transparent primitive come from the active draw mode.
  CHECK_EQ(packet.tpage, 0x0000u);
}

void test_flat_untextured_quad_has_four_vertices() {
  auto game = emptyGame();
  Core &core = game->core;
  writeTag(core, 5);
  // 0x28: four vertices over three (bit3 set).
  writeCommand(core, 0x28u, 0xbfbfffu);
  for (uint32_t vertex = 0; vertex < 4u; ++vertex) {
    writeXy(core, 2u + vertex, (int16_t)(250 + vertex), (int16_t)(105 + vertex));
  }
  spyro::gpu_packet_decode::Packet packet{};
  CHECK(decodes(core, 5, packet));
  CHECK_EQ(packet.vertexCount, 4u);
  CHECK(!packet.textured);
  CHECK(!packet.semiTransparent);
  CHECK_EQ(packet.vertices[3].sx, 253);
  CHECK_EQ(packet.vertices[3].rgb, 0x00bfbfffu);
}

// The flat textured family is refused by name: its word order is not established for this title,
// and a guessed vertex read silently relocates real geometry instead of reporting a gap.
void test_flat_textured_polygon_is_refused_by_name() {
  auto game = emptyGame();
  Core &core = game->core;
  const char *refusal = "none";
  spyro::gpu_packet_decode::Packet packet{};
  writeTag(core, 7);
  writeCommand(core, 0x24u, 0x00112233u);
  writeXy(core, 2, 100, 110);
  CHECK(!spyro::gpu_packet_decode::decode(&core, kPacket, 0u, 7, packet, refusal));
  CHECK(std::strcmp(refusal, "unsupported_flat_textured") == 0);
  // The untextured flat family stays decodable — that is the shaded pass's own code.
  writeTag(core, 4);
  writeCommand(core, 0x22u, 0x000073u);
  writeXy(core, 2, 251, 107);
  writeXy(core, 3, 254, 108);
  writeXy(core, 4, 252, 108);
  CHECK(decodes(core, 4, packet));
  CHECK_EQ(packet.code, 0x22u);
}

void test_textured_gouraud_triangle_still_reads_uv_and_texture_pages() {
  auto game = emptyGame();
  Core &core = game->core;
  writeTag(core, 9);
  writeCommand(core, 0x34u, 0x010203u);
  writeXy(core, 2, 1, 2);
  core.mem_w32(word(3), 0x33442211u); // vertex 0 UV: u=0x11 v=0x22 clut=0x3344
  core.mem_w32(word(4), 0x00445566u); // vertex 1 colour
  writeXy(core, 5, 3, 4);
  core.mem_w32(word(6), 0x77886655u); // vertex 1 UV: u=0x55 v=0x66 tpage=0x7788
  core.mem_w32(word(7), 0x00778899u); // vertex 2 colour
  writeXy(core, 8, 5, 6);
  core.mem_w32(word(9), 0x0000aa99u);
  spyro::gpu_packet_decode::Packet packet{};
  CHECK(decodes(core, 9, packet));
  CHECK(packet.textured);
  CHECK_EQ(packet.vertexCount, 3u);
  CHECK_EQ(packet.vertices[0].u, 0x11u);
  CHECK_EQ(packet.vertices[0].v, 0x22u);
  CHECK_EQ(packet.clut, 0x3344u);
  CHECK_EQ(packet.tpage, 0x7788u);
  CHECK_EQ(packet.vertices[2].rgb, 0x00778899u);
}

void test_non_polygon_and_mismatched_packets_are_refused_by_name() {
  auto game = emptyGame();
  Core &core = game->core;
  const char *refusal = "none";
  spyro::gpu_packet_decode::Packet packet{};
  writeTag(core, 4);
  writeCommand(core, 0x40u);
  CHECK(!spyro::gpu_packet_decode::decode(&core, kPacket, 0u, 4, packet, refusal));
  CHECK(std::strcmp(refusal, "unsupported_packet") == 0);
  // A flat triangle whose tag claims a quad's size must not be read as either.
  writeTag(core, 5);
  writeCommand(core, 0x20u);
  CHECK(!spyro::gpu_packet_decode::decode(&core, kPacket, 0u, 5, packet, refusal));
  CHECK(std::strcmp(refusal, "packet_size_mismatch") == 0);
}

} // namespace

int main() {
  RUN(gouraud_untextured_triangle_keeps_per_vertex_colour);
  RUN(flat_untextured_polygon_carries_one_colour);
  RUN(flat_untextured_quad_has_four_vertices);
  RUN(flat_textured_polygon_is_refused_by_name);
  RUN(textured_gouraud_triangle_still_reads_uv_and_texture_pages);
  RUN(non_polygon_and_mismatched_packets_are_refused_by_name);
  return pt_summary();
}
