#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::actor_model_codec {

struct Vec3i {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

enum class StreamStatus : uint8_t {
  Ok,
  ZeroVertices,
  FullUnderflow,
  DeltaUnderflow,
  TrailingInput,
};

struct StreamInput {
  uint32_t firstFull = 0;
  std::span<const uint32_t> fullWords;
  std::span<const int16_t> deltaWords;
  uint32_t vertexCount = 0;
  uint8_t assetShift = 0;
};

struct StreamResult {
  StreamStatus status = StreamStatus::ZeroVertices;
  std::vector<Vec3i> vertices;
};

// Decode the 0x8001F798 packed model stream. The low bit of each consumed
// source word selects and advances the full-word or signed-16 stream for the
// following vertex. Input spans must be exact; underflow and trailing words
// are refused rather than inferred.
StreamResult decodeStream(const StreamInput &input);

struct BlendResult {
  std::array<int32_t, 3> mac{};
  std::array<int16_t, 3> ir{};
};

// Semantic INTPL used by 0x8001F798 and the paired actor. Factor is the exact
// signed IR0-domain value; output pose is MAC, while IR is retained for
// endpoint verification.
BlendResult blendPose(Vec3i primary, Vec3i alternate, int16_t factor);

struct DepthCueResult {
  uint32_t rgb = 0;
  std::array<int32_t, 3> mac{};
  std::array<int16_t, 3> ir{};
};

// Semantic sf=1,lm=0 DPCS used by the reached PositiveBlend color arm.
// The upper input byte is preserved as the GP0 command/code byte.
DepthCueResult depthCueRgb(uint32_t rgb, std::array<int32_t, 3> farColor, int16_t factor);

} // namespace spyro::actor_model_codec
