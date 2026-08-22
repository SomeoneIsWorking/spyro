#include "world_chunk_codec.h"

#include <limits>

namespace spyro::world_chunk_codec {
namespace {

bool add(uint32_t left, uint32_t right, uint32_t &out) {
  if (right > std::numeric_limits<uint32_t>::max() - left) {
    return false;
  }
  out = left + right;
  return true;
}

} // namespace

bool RamView::contains(uint32_t address, uint32_t size) const {
  const bool mapped = address < 0x00200000u || (address >= 0x80000000u && address < 0x80200000u);
  const uint32_t physical = address & 0x1fffffffu;
  return mapped && physical <= mBytes.size() && size <= mBytes.size() - physical;
}

uint8_t RamView::r8(uint32_t address) const {
  return mBytes[address & 0x1fffffffu];
}

uint16_t RamView::r16(uint32_t address) const {
  return (uint16_t)r8(address) | (uint16_t)((uint16_t)r8(address + 1u) << 8);
}

uint32_t RamView::r32(uint32_t address) const {
  return (uint32_t)r16(address) | ((uint32_t)r16(address + 2u) << 16);
}

Status decodeLow(const RamView &ram, uint32_t address, LowChunk &out) {
  out = {};
  if ((address & 3u) || !ram.contains(address, 0x1cu)) {
    return Status::HeaderBounds;
  }
  out.address = address;
  out.originWord = ram.r32(address + 8u);
  out.originZ = ram.r16(address + 0x0eu);
  out.descriptor = ram.r32(address + 0x10u);
  const uint32_t vertexCount = out.descriptor & 0xffu;
  const uint32_t colorCount = (out.descriptor >> 8) & 0xffu;
  const uint32_t faceCount = (out.descriptor >> 16) & 0xffu;
  if (!vertexCount || vertexCount > 256u || colorCount > 256u) {
    return Status::InvalidCount;
  }
  uint32_t colorBase = 0, faceBase = 0, end = 0;
  if (!add(address + 0x1cu, vertexCount * 4u, colorBase) ||
      !add(colorBase, colorCount * 4u, faceBase) || !add(faceBase, faceCount * 8u, end)) {
    return Status::LayoutOverflow;
  }
  if (!ram.contains(address + 0x1cu, end - (address + 0x1cu))) {
    return Status::PayloadBounds;
  }
  out.vertices.reserve(vertexCount);
  out.colors.reserve(colorCount);
  out.faces.reserve(faceCount);
  for (uint32_t i = 0; i < vertexCount; ++i) {
    out.vertices.push_back(ram.r32(address + 0x1cu + i * 4u));
  }
  for (uint32_t i = 0; i < colorCount; ++i) {
    out.colors.push_back(ram.r32(colorBase + i * 4u));
  }
  for (uint32_t i = 0; i < faceCount; ++i) {
    const uint32_t source = faceBase + i * 8u;
    out.faces.push_back({source, ram.r32(source), ram.r32(source + 4u)});
  }
  return Status::Ok;
}

Status decodeHigh(const RamView &ram, uint32_t address, HighChunk &out) {
  out = {};
  if ((address & 3u) || !ram.contains(address, 0x1cu)) {
    return Status::HeaderBounds;
  }
  out.address = address;
  out.originWord = ram.r32(address + 8u);
  out.originAndOffset = ram.r32(address + 0x0cu);
  out.layout = ram.r32(address + 0x14u);
  const uint32_t vertexCount = out.layout & 0xffu;
  const uint32_t planeBytes = (out.layout >> 6) & 0x3fcu;
  const uint32_t faceBytes = (out.layout >> 12) & 0xff0u;
  const uint32_t vertexOffset = (out.layout >> 22) & 0x3fcu;
  if (!vertexCount || (planeBytes & 3u) || (faceBytes & 15u)) {
    return Status::InvalidCount;
  }
  uint32_t vertexBase = 0, farBase = 0, nearBase = 0, faceBase = 0, end = 0;
  if (!add(address + 0x1cu, vertexOffset, vertexBase) ||
      !add(vertexBase, vertexCount * 4u, farBase)) {
    return Status::LayoutOverflow;
  }
  if (!add(farBase, planeBytes, nearBase) || !add(nearBase, planeBytes, faceBase) ||
      !add(faceBase, faceBytes, end)) {
    return Status::LayoutOverflow;
  }
  if (!ram.contains(vertexBase, end - vertexBase)) {
    return Status::PayloadBounds;
  }
  out.vertices.reserve(vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i) {
    out.vertices.push_back(ram.r32(vertexBase + i * 4u));
  }
  out.farColors.reserve(planeBytes / 4u);
  out.nearColors.reserve(planeBytes / 4u);
  for (uint32_t i = 0; i < planeBytes; i += 4u) {
    out.farColors.push_back(ram.r32(farBase + i));
    out.nearColors.push_back(ram.r32(nearBase + i));
  }
  out.faces.reserve(faceBytes / 16u);
  for (uint32_t source = faceBase; source < end; source += 16u) {
    out.faces.push_back({source,
                         ram.r32(source),
                         ram.r32(source + 4u),
                         ram.r32(source + 8u),
                         ram.r32(source + 12u)});
  }
  return Status::Ok;
}

} // namespace spyro::world_chunk_codec
