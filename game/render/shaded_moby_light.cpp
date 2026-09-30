#include "shaded_moby_light.h"

#include <algorithm>

namespace spyro::shaded_light {
namespace {

// GTE MVMVA with sf=1, lm=0: each row against the three IR values, arithmetic shift by 12, then the
// 16-bit signed saturation every IR write applies.
std::array<std::int32_t, 3> rotate(const Matrix3 &matrix, std::array<std::int32_t, 3> ir) {
  std::array<std::int32_t, 3> out{};
  for (std::size_t row = 0; row < 3; ++row) {
    std::int64_t sum = 0;
    for (std::size_t column = 0; column < 3; ++column) {
      sum += (std::int64_t)matrix[row][column] * ir[column];
    }
    out[row] = (std::int32_t)std::clamp<std::int64_t>(sum >> 12, -32768, 32767);
  }
  return out;
}

// The background colour CC adds, RBK/GBK/BBK: the entry's three bytes, each scaled by 16
// (0x80023154, 0x80023170-0x80023178).
std::array<std::int32_t, 3> background(std::uint32_t entry) {
  return {(std::int32_t)((entry << 4) & 0xff0u),
          (std::int32_t)((entry >> 4) & 0xff0u),
          (std::int32_t)((entry >> 12) & 0xff0u)};
}

} // namespace

NormalIr vertexNormal(std::array<std::int8_t, 3> memoryOrder) {
  return {(std::int32_t)memoryOrder[1], (std::int32_t)memoryOrder[2], (std::int32_t)memoryOrder[0]};
}

NormalIr faceNormal(std::uint32_t word) {
  return {(std::int32_t)(std::int8_t)(word >> 16),
          (std::int32_t)(std::int8_t)(word >> 8),
          (std::int32_t)(std::int8_t)(word >> 24)};
}

GteStage gteStage(const Input &input, const NormalIr &normal) {
  std::array<std::int32_t, 3> ir = rotate(input.rotation, normal);
  // GPF 0 with IR0 = entry >> 23 & 0x1E (0x80023168-0x8002316C, 0x8002317C): sf=0, so the product
  // is not shifted, and it saturates back into IR as a signed 16-bit value.
  const std::int32_t scale = (std::int32_t)((input.entry >> 23) & 0x1eu);
  for (std::int32_t &value : ir) {
    value = std::clamp(value * scale, -32768, 32767);
  }
  // CC, sf=1, lm=1, with the colour register loaded as 0x00FFFFFF (0x80023180-0x80023198). It runs
  // two phases and writes IR after BOTH: first the lit brightness (background plus light matrix
  // times IR), then that brightness times the colour register's 255 (`R*IR << 4 >> 12`), whose
  // accumulators are the colour FIFO bytes (`/ 16`) and end up in IR. The highlight step reads the
  // SECOND IR, so it is 255/256 of the first one; MEASURED against the GTE reference, which is why
  // this test exists.
  const auto bk = background(input.entry);
  GteStage stage{};
  for (std::size_t row = 0; row < 3; ++row) {
    std::int64_t sum = (std::int64_t)bk[row] << 12;
    for (std::size_t column = 0; column < 3; ++column) {
      sum += (std::int64_t)input.colourMatrix[row][column] * ir[column];
    }
    const std::int64_t lit = std::clamp<std::int64_t>(sum >> 12, 0, 32767);
    const std::int64_t mac = ((std::int64_t)255 * lit << 4) >> 12;
    stage.ir[row] = (std::int32_t)std::clamp<std::int64_t>(mac, 0, 32767);
    stage.rgb |= (std::uint32_t)std::clamp<std::int64_t>(mac >> 4, 0, 255) << (8 * row);
  }
  return stage;
}

std::uint32_t highlight(std::uint32_t entry, const GteStage &stage) {
  const std::int32_t nibble = (std::int32_t)(entry >> 28);
  if (nibble == 0) {
    return stage.rgb;
  }
  const std::int32_t excess = stage.ir[0] - (nibble << 7) - background(entry)[0];
  if (excess <= 0) {
    return stage.rgb;
  }
  std::uint32_t out = 0;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const std::int32_t value = stage.ir[channel] + excess * 2;
    const std::uint32_t byte = value >= 0x1000 ? 0xffu : (std::uint32_t)value >> 4;
    out |= byte << (8 * channel);
  }
  return out;
}

std::uint32_t vertexColour(std::uint32_t entry, const GteStage &stage) {
  const std::uint32_t floor = entry & 0xffffffu;
  return stage.rgb < floor ? floor : highlight(entry, stage);
}

std::vector<std::uint32_t> vertexColours(const Input &input,
                                         std::span<const std::array<std::int8_t, 3>> normals) {
  std::vector<std::uint32_t> out;
  out.reserve(normals.size());
  for (const auto &normal : normals) {
    out.push_back(vertexColour(input.entry, gteStage(input, vertexNormal(normal))));
  }
  return out;
}

std::uint32_t faceColour(const Input &input, std::uint32_t normalWord) {
  return highlight(input.entry, gteStage(input, faceNormal(normalWord)));
}

} // namespace spyro::shaded_light
