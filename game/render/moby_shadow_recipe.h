#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Core;

namespace spyro::moby_shadow_recipe {

// Retail's handwritten 0x80059F8C fans four triangles around one projected anchor per shadow, where
// the Spyro shadow 0x80059A48 fans sixteen. The two renderers are siblings in the same source file
// but differ in ring size, materials and depth scale, so they stay separate recipes.
constexpr std::size_t kFanPoints = 4;

enum class Status : std::uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidState,
  InvalidProjection,
};

// Why a shadow in the list produced no face. Counted rather than dropped silently: a frame where
// every shadow is rejected has to be distinguishable from a frame with no shadows queued.
enum class Reject : std::uint8_t {
  None,
  NoShadowPlane, // the low half of m_ShadowDistance is zero
  BehindCamera,  // the anchor's view Z reached the 0x1000 far limit
  Backfacing,    // NCLIP said the fan winds away from the viewer
  OffScreen,     // the anchor left the retained screen window
  NegativeBin,   // every fan triangle sorted in front of the ordering table
};

struct Vertex {
  std::int16_t sx = 0;
  std::int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
  std::uint16_t sz = 0;
};

struct Face {
  std::array<Vertex, 3> vertices{};
  std::uint16_t otBin = 0;
  std::uint32_t fanOrdinal = 0;
  std::uint32_t moby = 0;
  // The distance fade retail bakes into the packet's colour word.
  std::uint8_t grey = 0x80u;
};

struct Recipe {
  Status status = Status::InvalidState;
  std::vector<Face> faces;
  std::uint32_t entries = 0;
  std::uint32_t drawn = 0;
  std::array<std::uint32_t, 6> rejects{};
};

// ((first + second) * 3/2 + anchor) >> shift, minus the bias. 0x80059F8C shifts by 7 over its
// four-point ring where 0x80059A48 shifts by 9 over sixteen, so the shift is a parameter and not a
// constant shared between them.
std::int32_t otBin(std::uint16_t firstSz,
                   std::uint16_t secondSz,
                   std::uint16_t anchorSz,
                   std::int32_t bias,
                   std::uint32_t shift);

// The greyscale retail writes into the packet colour. Full 0x80 inside 0xC00 of view depth, then a
// linear ramp to zero at the 0x1000 far limit.
std::uint8_t distanceGrey(std::int32_t anchorViewZ);

// Consume the owned scene projection published in Core::rsub.projParams, as the sibling Spyro
// shadow does, rather than whatever the guest left in the GTE.
Recipe derive(Core *core);
const char *statusName(Status status);

} // namespace spyro::moby_shadow_recipe
