#include "gpu_packet_decode.h"

#include "core.h"

namespace spyro::gpu_packet_decode {
namespace {

constexpr uint32_t kseg(uint32_t address) {
  return 0x80000000u | (address & 0x1fffffu);
}

bool ramSpan(uint32_t address, uint32_t bytes) {
  const uint32_t physical = address & 0x1fffffffu;
  return (address & 3u) == 0u && bytes != 0u && physical <= 0x200000u - bytes;
}

bool refuse(const char *why, const char *&refusal) {
  refusal = why;
  return false;
}

} // namespace

bool decode(Core *core,
            uint32_t packet,
            uint16_t drawMode,
            uint8_t wordCount,
            Packet &out,
            const char *&refusal) {
  refusal = "none";
  if (!ramSpan(packet, 8u)) {
    return refuse("packet_out_of_ram", refusal);
  }
  const uint8_t code = (uint8_t)(core->mem_r32(kseg(packet + 4u)) >> 24);
  out = {};
  out.address = kseg(packet);
  out.code = code;
  // Polygon commands only (0x20..0x3F: bit5 set, bits 6-7 clear). The geometry bits are the same
  // for every family: bit4 selects Gouraud (a colour per vertex) over flat (one colour for the
  // whole polygon), bit3 four vertices over three, bit2 a texture, bit1 semi-transparency. The
  // title uses both families in one frame — the world producers emit Gouraud polygons and
  // func_80022A2C's shaded pass emits flat ones — so refusing the flat half silently reports a
  // drawing pass as silent.
  if ((code & 0xe0u) != 0x20u) {
    return refuse("unsupported_packet", refusal);
  }
  const bool gouraud = (code & 0x10u) != 0u;
  out.vertexCount = (code & 0x08u) != 0u ? 4u : 3u;
  out.textured = (code & 0x04u) != 0u;
  out.semiTransparent = (code & 0x02u) != 0u;
  // The flat textured family is refused by name. Its three-vertex size (32 bytes) is fixed but the
  // order of the colour, XY and texcoord words is not established for this title, and guessing
  // XY/texcoord interleaving moved 16 retail primitives out of the matched set and broke 8
  // shadow-arm matches: a wrong vertex read silently relocates real geometry, which is worse than a
  // counted refusal. Establish the layout from the code that writes these packets before adding
  // them; the oracle census prints the refused command codes, so the gap stays visible until then.
  if (out.textured && !gouraud) {
    return refuse("unsupported_flat_textured", refusal);
  }
  // Only Gouraud carries a colour per vertex; a flat polygon carries one before the vertex list.
  const uint32_t colourWords = gouraud ? out.vertexCount : 1u;
  const uint32_t packetBytes =
      4u + 4u * colourWords + 4u * out.vertexCount + (out.textured ? 4u * out.vertexCount : 0u);
  if (wordCount != packetBytes / 4u - 1u || !ramSpan(packet, packetBytes)) {
    return refuse("packet_size_mismatch", refusal);
  }
  // Vertex stride: Gouraud interleaves a colour with every vertex (and this title's textured
  // Gouraud packets keep the texcoord with its vertex); a flat untextured polygon carries its
  // single colour before the vertex list and then one XY pair per vertex.
  const uint32_t stride = out.textured ? 12u : (gouraud ? 8u : 4u);
  for (uint32_t vertex = 0; vertex < out.vertexCount; ++vertex) {
    Vertex &v = out.vertices[vertex];
    const uint32_t rgbAddress = gouraud ? packet + 4u + vertex * stride : packet + 4u;
    const uint32_t xyAddress = packet + 8u + vertex * stride;
    const uint32_t xy = core->mem_r32(kseg(xyAddress));
    v.sx = (int16_t)xy;
    v.sy = (int16_t)(xy >> 16);
    v.rgb = core->mem_r32(kseg(rgbAddress)) & 0x00ffffffu;
    if (!out.textured) {
      continue;
    }
    const uint32_t uv = core->mem_r32(kseg(xyAddress + 4u));
    v.u = (uint8_t)uv;
    v.v = (uint8_t)(uv >> 8);
    if (vertex == 0) {
      out.clut = (uint16_t)(uv >> 16);
    } else if (vertex == 1) {
      out.tpage = (uint16_t)(uv >> 16);
    }
  }
  if (!out.textured && out.semiTransparent) {
    out.tpage = drawMode & 0x60u;
  }
  return true;
}

} // namespace spyro::gpu_packet_decode
