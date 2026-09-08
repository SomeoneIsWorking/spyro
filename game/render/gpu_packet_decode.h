#pragma once

#include <array>
#include <cstdint>

class Core;

namespace spyro::gpu_packet_decode {

// One vertex exactly as a linked retail GPU packet carries it. Nothing here is inferred: view
// depth, source address and builder provenance are absent because a packet does not carry them.
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

// Decode one linked G3/GT3/G4/GT4 packet. `wordCount` is the tag's own word count and is checked
// against the family's packet size, so a mis-walked chain refuses by name instead of producing a
// plausible record. `drawMode` supplies the blend bits an untextured semi-transparent primitive
// inherits from the active draw mode rather than carrying itself.
bool decode(Core *core,
            uint32_t packet,
            uint16_t drawMode,
            uint8_t wordCount,
            Packet &out,
            const char *&refusal);

} // namespace spyro::gpu_packet_decode
