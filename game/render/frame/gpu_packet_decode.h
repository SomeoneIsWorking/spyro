#pragma once

#include <array>
#include <cstdint>

class Core;

namespace spyro::gpu_packet_decode {

// One vertex exactly as a linked retail GPU packet carries it: no view depth, source address or
// builder provenance, because a packet does not carry them.
struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  uint32_t rgb = 0;
  uint8_t u = 0;
  uint8_t v = 0;
};

struct Packet {
  uint32_t address = 0;
  uint8_t code = 0;
  uint8_t vertexCount = 3;
  bool textured = false;
  bool semiTransparent = false;
  uint16_t clut = 0;
  uint16_t tpage = 0;
  std::array<Vertex, 4> vertices{};
};

// Decode one linked G3/GT3/G4/GT4 packet. `wordCount` is checked against the family's packet size,
// so a mis-walked chain refuses by name; `drawMode` supplies an untextured prim's blend bits.
bool decode(Core *core,
            uint32_t packet,
            uint16_t drawMode,
            uint8_t wordCount,
            Packet &out,
            const char *&refusal);

} // namespace spyro::gpu_packet_decode
