#pragma once

#include "actor_model_codec.h"
#include "native_projection.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spyro::actor_prefix {

enum class ColorArm : uint8_t { High, PositiveBlend, Plain, NegativeBlend };

struct OwnedStream {
  uint32_t firstFull = 0;
  std::vector<uint32_t> fullWords;
  std::vector<int16_t> deltaWords;
};

struct Input {
  uint32_t header = 0;
  int32_t tx = 0;
  int32_t ty = 0;
  int32_t tz = 0;
  std::array<uint32_t, 5> matrixWords{};
  int32_t cr29 = 0;
  int16_t cr30 = 0;
  uint8_t transformShift = 0;
  uint8_t streamShift = 0;
  bool optionalExpansion = false;
  uint32_t vertexCount = 0;
  OwnedStream primary;
  OwnedStream alternate;
  ColorArm colorArm = ColorArm::High;
  std::vector<uint32_t> primaryColors;
  std::vector<uint32_t> secondaryColors;
  std::vector<uint32_t> primitiveWords;
  psxport::native_projection::ProjectionParams projection;
};

enum class Status : uint8_t {
  Ok,
  OptionalExpansion,
  TransformBlend,
  ClipStatus,
  CountZero,
  Stream,
  PlainColor,
  NegativeBlend,
  ColorCount,
};

struct Vertex {
  actor_model_codec::Vec3i primary;
  actor_model_codec::Vec3i alternate;
  actor_model_codec::Vec3i resolved;
  psxport::native_projection::ModelVertex projectionInput;
  psxport::native_projection::NativeProjectedVertex projected;
};

struct Output {
  Status status = Status::Stream;
  std::array<uint32_t, 16> controls{}; // CR0..7 and CR13..15 at their numeric indices.
  std::vector<Vertex> vertices;
  std::vector<uint32_t> colors;
  std::vector<uint32_t> primitiveWords;
};

struct CompareResult {
  uint32_t vertices = 0;
  uint32_t colors = 0;
  uint32_t primitiveWords = 0;
  uint32_t mismatches = 0;
  const char *firstField = "none";
};

// Pure reached-prefix builder. Input is an immutable semantic deep copy: no
// Core, guest addresses, scratch products, GTE state, or opcodes. Optional
// expansion, clip/status and unreached color arms are explicit refusals.
Output build(const Input &input);

// Hermetic exact-output comparator for builder tests. The live diagnostic
// compares independently observed guest state at qualified checkpoints.
CompareResult compareOutputs(const Output &expected, const Output &actual);

} // namespace spyro::actor_prefix
