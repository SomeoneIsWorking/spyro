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
  switch (code & (uint8_t)~2u) {
  case 0x30:
    out.vertexCount = 3;
    break;
  case 0x34:
    out.vertexCount = 3;
    out.textured = true;
    break;
  case 0x38:
    out.vertexCount = 4;
    break;
  case 0x3c:
    out.vertexCount = 4;
    out.textured = true;
    break;
  default:
    return refuse("unsupported_packet", refusal);
  }
  const uint32_t packetBytes =
      out.textured ? (out.vertexCount == 4 ? 52u : 40u) : (out.vertexCount == 4 ? 36u : 28u);
  if (wordCount != packetBytes / 4u - 1u || !ramSpan(packet, packetBytes)) {
    return refuse("packet_size_mismatch", refusal);
  }
  out.semiTransparent = (code & 2u) != 0;
  const uint32_t stride = out.textured ? 12u : 8u;
  for (uint32_t vertex = 0; vertex < out.vertexCount; ++vertex) {
    const uint32_t rgbAddress = packet + 4u + vertex * stride;
    const uint32_t xyAddress = rgbAddress + 4u;
    const uint32_t xy = core->mem_r32(kseg(xyAddress));
    Vertex &v = out.vertices[vertex];
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
