#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spyro::field_shaded_queue_recipe {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidInput,
  InvalidOtBin,
};

struct Primitive {
  uint32_t indices = 0;
  uint32_t normal = 0;
  std::array<uint32_t, 4> vertexColours{};
};

struct Record {
  uint32_t actor = 0;
  uint32_t actorOrdinal = 0;
  uint16_t meshIndex = 0;
  bool clipMode = false;
  uint32_t lightBase = 0;
  uint32_t lightScale = 0;
  // Variant 1's arm indexes a different table from the same guest word (`r_moby.s` 0x80023268 loads
  // LO from the record's +0x4C once per Moby; 0x8006E44C + (LO >> 21) is variant 3's pair, while
  // 0x8006E3D8 + (LO >> 22) is ONE word whose top bits are the GPF scale).
  uint32_t lightEntry = 0;
  int32_t lightEntryIndex = 0;
  psxport::native_projection::FixedAffine affine{};
  std::vector<psxport::native_projection::ModelVertex> vertices;
  std::vector<Primitive> primitives;
};

struct Input {
  psxport::native_projection::ProjectionParams projection{};
  std::array<std::array<int16_t, 3>, 3> colourMatrix{};
  int32_t clipRight = 512;
  std::vector<Record> records;
};

struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  uint16_t sz = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
};

struct Face {
  uint32_t actor = 0;
  uint32_t actorOrdinal = 0;
  uint32_t primitiveOrdinal = 0;
  uint32_t paintGroup = 0;
  uint16_t otBin = 0;
  uint8_t vertexCount = 0;
  bool semiTransparent = false;
  bool gouraud = false;
  std::array<uint32_t, 4> rgb{};
  std::array<Vertex, 4> vertices{};
};

struct Recipe {
  Status status = Status::ValidEmpty;
  uint32_t sourceRecords = 0;
  uint32_t candidates = 0;
  uint32_t rejected = 0;
  uint32_t firstUnsupportedActor = 0;
  uint32_t firstUnsupportedPrimitive = 0;
  std::vector<Face> faces;
};

Recipe derive(const Input &input);

// Named so a refusal reports WHICH condition failed rather than a bare enum value.
const char *statusName(Status status);

} // namespace spyro::field_shaded_queue_recipe
