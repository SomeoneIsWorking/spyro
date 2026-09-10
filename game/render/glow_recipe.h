#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Core;

namespace spyro::glow_recipe {

// Retail's 0x80058BA8 is a two-line C function calling the handwritten glow renderer 0x800580F4 and
// then the sparkle renderer 0x800584C4. This recipe owns the first: sixteen fixed records at
// 0x80078800, each a fan of semi-transparent Gouraud triangles from one bright projected centre out
// to a ring of black points, which is how the game draws a soft additive halo without a texture.
constexpr std::size_t kRecords = 16;
constexpr std::uint32_t kRecordStride = 0x24u;

enum class Status : std::uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidState,
  InvalidProjection,
};

// Why a record produced no triangle. Counted rather than dropped silently: a frame where every glow
// is rejected has to be distinguishable from a frame with no glows registered.
enum class Reject : std::uint8_t {
  None,
  EmptyRecord, // the point count at +0x00 is zero, which is how a glow is switched off
  NoDepth,     // the restored view depth is zero, so the radius division has no answer
  NegativeBin, // the ordering-table bin landed at or in front of the table's front
  Offscreen,   // centre and both ring points fell outside the same screen edge
  Count,
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
};

struct Face {
  std::array<Vertex, 3> vertices{};
  // The record's colour, at the centre only: retail writes both ring vertices black so the fan
  // fades out on its own.
  std::uint32_t colour = 0;
  std::uint16_t otBin = 0;
  // The record's index, not its address: the painter order encodes it, and a guest pointer does not
  // fit the ordinal field.
  std::uint32_t recordIndex = 0;
  std::uint32_t fanOrdinal = 0;
};

struct Recipe {
  Status status = Status::InvalidState;
  std::vector<Face> faces;
  std::uint32_t records = 0; // records with a non-zero point count
  std::uint32_t drawn = 0;   // records that produced at least one triangle
  std::array<std::uint32_t, (std::size_t)Reject::Count> rejects{};
};

// The screen-edge outcode retail builds per vertex and then ANDs across the triangle: a non-zero
// result means all three are outside one edge and the triangle is dropped. `right` is the screen's
// own right edge: retail's is always 512, but a widescreen frame is wider and a glow past 512 is
// then on screen, not off it.
std::uint32_t outcode(std::int32_t x, std::int32_t y, std::int32_t right);
// The ordering-table bin: the restored view depth shifted right seven, biased by the record's own
// offset, pushed 0x40 further back past 0xFF and clamped at the table's last bin.
std::int32_t otBin(std::uint32_t viewZ, std::int32_t bias);

Recipe derive(Core *core);
const char *statusName(Status status);

} // namespace spyro::glow_recipe
