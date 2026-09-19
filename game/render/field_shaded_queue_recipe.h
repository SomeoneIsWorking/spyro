#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>
#include <span>
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
  // Records projected through an interval rather than their own transform, and records that had a
  // predecessor but whose interval the framework refused. Both are zero on a logic frame; in a
  // sampled frame `sampled == 0` with a nonzero denominator is a rule that ran and matched nothing,
  // which a single counter could not distinguish from one that never ran.
  uint32_t sampled = 0;
  uint32_t sampleDeclined = 0;
  uint32_t firstUnsupportedActor = 0;
  uint32_t firstUnsupportedPrimitive = 0;
  std::vector<Face> faces;
};

// Where a record's geometry is sampled from. Without one, every record is projected through its
// own transform, which is the logic frame's own picture. With one, a record that has a paired
// predecessor is projected through the interval between the two transforms, in view space, before
// projection — never by blending two already-projected vertices, which would blend two saturated,
// wrapped results and could not recover a rotation.
//
// `previous[i]` is the predecessor of `input.records[i]`, or nullptr where that record was
// unpaired. A record whose interval the framework refuses falls back to its own transform: the
// picture the logic frame would have shown is strictly closer than replaying the previous one.
struct Interval {
  std::span<const Record *const> previous;
  double t = 1.0;
};

Recipe derive(const Input &input, const Interval *interval = nullptr);

// Named so a refusal reports WHICH condition failed rather than a bare enum value.
const char *statusName(Status status);

} // namespace spyro::field_shaded_queue_recipe
