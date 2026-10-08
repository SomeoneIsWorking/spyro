#include "actor_model_codec.h"

#include <algorithm>

namespace spyro::actor_model_codec {
namespace {

int64_t wrap44(int64_t value) {
  constexpr uint64_t mask = (UINT64_C(1) << 44) - 1;
  constexpr uint64_t sign = UINT64_C(1) << 43;
  const uint64_t bits = (uint64_t)value & mask;
  return bits < sign ? (int64_t)bits : (int64_t)bits - (INT64_C(1) << 44);
}

int32_t sar32(uint32_t value, unsigned shift) {
  return (int32_t)value >> shift;
}

int32_t wrapAdd(int32_t left, int32_t right) {
  return (int32_t)((uint32_t)left + (uint32_t)right);
}

int32_t scaleComponent(int32_t value, uint8_t shift) {
  return (int32_t)((uint32_t)value << shift);
}

Vec3i decodeFull(uint32_t word) {
  return {sar32(word, 21), sar32(word << 10, 21), sar32(word << 20, 19)};
}

Vec3i applyDelta(Vec3i current, int16_t delta, uint8_t shift) {
  const uint32_t word = (uint32_t)(int32_t)delta;
  const int32_t dx = scaleComponent((int32_t)delta >> 11, shift);
  const int32_t dy = scaleComponent(-sar32(word << 21, 27), shift);
  const int32_t dz = scaleComponent(-sar32(word << 26, 27), shift);
  return {wrapAdd(current.x, dx), wrapAdd(current.y, dy), wrapAdd(current.z, dz)};
}

int32_t interpolate(int32_t from, int32_t to, int16_t factor) {
  const int64_t first = wrap44((int64_t)to * 4096 - (int64_t)(int32_t)((uint32_t)from << 12));
  const int32_t difference = std::clamp((int32_t)(first >> 12), -32768, 32767);
  return (int32_t)(wrap44((int64_t)from * 4096 + (int32_t)factor * difference) >> 12);
}

} // namespace

StreamResult decodeStream(const StreamInput &input) {
  if (input.vertexCount == 0) {
    return {StreamStatus::ZeroVertices, {}};
  }
  StreamResult out{StreamStatus::Ok, {}};
  out.vertices.reserve(input.vertexCount);
  Vec3i current = decodeFull(input.firstFull);
  out.vertices.push_back(current);
  uint32_t selector = input.firstFull;
  size_t fullCursor = 0;
  size_t deltaCursor = 0;
  const uint8_t shift = input.assetShift & 31u;
  while (out.vertices.size() < input.vertexCount) {
    if (selector & 1u) {
      if (deltaCursor == input.deltaWords.size()) {
        return {StreamStatus::DeltaUnderflow, {}};
      }
      const int16_t word = input.deltaWords[deltaCursor++];
      current = applyDelta(current, word, shift);
      selector = (uint16_t)word;
    } else {
      if (fullCursor == input.fullWords.size()) {
        return {StreamStatus::FullUnderflow, {}};
      }
      const uint32_t word = input.fullWords[fullCursor++];
      current = decodeFull(word);
      selector = word;
    }
    out.vertices.push_back(current);
  }
  if (fullCursor != input.fullWords.size() || deltaCursor != input.deltaWords.size()) {
    return {StreamStatus::TrailingInput, {}};
  }
  return out;
}

BlendResult blendPose(Vec3i primary, Vec3i alternate, int16_t factor) {
  BlendResult out{};
  // The caller writes primary coordinates to IR1..3 (signed low half) and
  // alternate coordinates to the full-width FC control vector.
  const int32_t from[] = {(int16_t)primary.x, (int16_t)primary.y, (int16_t)primary.z};
  const int32_t to[] = {alternate.x, alternate.y, alternate.z};
  for (unsigned i = 0; i < 3; ++i) {
    out.mac[i] = interpolate(from[i], to[i], factor);
    out.ir[i] = (int16_t)std::clamp(out.mac[i], -32768, 32767);
  }
  return out;
}

DepthCueResult depthCueRgb(uint32_t rgb, std::array<int32_t, 3> farColor, int16_t factor) {
  DepthCueResult out{};
  const int32_t channel[] = {
      (int32_t)(rgb & 0xffu) << 4,
      (int32_t)((rgb >> 8) & 0xffu) << 4,
      (int32_t)((rgb >> 16) & 0xffu) << 4,
  };
  uint32_t packed = rgb & 0xff000000u;
  for (unsigned i = 0; i < 3; ++i) {
    out.mac[i] = interpolate(channel[i], farColor[i], factor);
    out.ir[i] = (int16_t)std::clamp(out.mac[i], -32768, 32767);
    const uint32_t result = (uint32_t)std::clamp(out.mac[i] >> 4, 0, 255);
    packed |= result << (i * 8u);
  }
  out.rgb = packed;
  return out;
}

} // namespace spyro::actor_model_codec
