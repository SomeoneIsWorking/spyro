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
  UnsupportedVariant,
  UnsupportedLighting,
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
  int32_t lightingOffset = 0;
  uint32_t lightBase = 0;
  uint32_t lightScale = 0;
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

} // namespace spyro::field_shaded_queue_recipe
