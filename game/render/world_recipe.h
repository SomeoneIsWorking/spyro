#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::world_recipe {

enum class Family : uint8_t { G3, G4, GT3, GT4 };
enum class Origin : uint8_t { LowDirect, HighDirect, Medium, Near, Adaptive, EdgeFiller };
enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  ActiveAnimation,
  InvalidSelection,
  InvalidSector,
  InvalidChunk,
  InvalidMaterial,
  CapacityExceeded,
  UnsupportedSubdivision,
};

struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  uint16_t sz = 0;
  uint8_t clip = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
  uint32_t rgb = 0;
  uint8_t u = 0;
  uint8_t v = 0;
};

struct Material {
  bool textured = false;
  bool semiTransparent = false;
  uint16_t clut = 0;
  uint16_t tpage = 0;
};

struct Face {
  Family family = Family::G3;
  Origin origin = Origin::LowDirect;
  uint8_t vertexCount = 3;
  uint16_t otBin = 0;
  uint32_t sector = 0;
  uint32_t source = 0;
  uint32_t sourceOrdinal = 0;
  // Address of the authored texture words used for this face. Refinement and
  // adaptive descendants retain it so an oracle mismatch can be traced back
  // to the exact material-table input rather than inferred from decoded UVs.
  uint32_t textureSource = 0;
  // Every packet linked into the guest OT receives a monotonically increasing
  // group. Adaptive subdivision replaces one linked parent in place, so all
  // of its final descendants retain that parent's group and use suborder to
  // describe the replacement chain.
  uint32_t paintGroup = UINT32_MAX;
  uint32_t paintSuborder = 0;
  std::array<Vertex, 4> vertices{};
  Material material{};
};

struct Recipe {
  Status status = Status::ValidEmpty;
  const char *refusal = "none";
  std::array<uint8_t, 256> broadVisible{};
  std::vector<Face> faces;
  uint32_t selectedSectors = 0;
  uint32_t lowSectors = 0;
  uint32_t highSectors = 0;
  uint32_t candidates = 0;
  uint32_t rejected = 0;
  uint32_t nextPaintGroup = 0;
};

uint8_t clipCode(int16_t sx, int16_t sy, int clipRight = 512);
Vertex midpoint(const Vertex &left, const Vertex &right, int clipRight = 512);

// The final 0x8002A0A0 adaptive step. It is deliberately independent of the
// chunk decoder: medium/near refinement and the runtime span limiter share one
// midpoint implementation and one child ordering authority.
bool adaptiveSubdivide(const Face &parent,
                       std::vector<Face> &out,
                       size_t faceLimit,
                       int clipRight = 512);

// Reserve the OT-link identity of one packet. A normal face consumes the
// group when it is appended; an oversized near face reserves it before the
// packet is recursively replaced in place.
uint32_t reservePaintGroup(Recipe &recipe);

// Append one final linked face, assigning a fresh paint group when this is not
// an adaptive descendant with an inherited group.
bool appendLinked(Recipe &recipe, Face face, size_t faceLimit);

// Reconstruct the guest's final DMA replay order: descending OT buckets,
// AddPrim LIFO between root packets, and recursive in-place child order within
// an adaptively replaced packet.
bool paintOrder(std::span<const Face> faces, std::vector<size_t> &indices);

struct Difference {
  bool equal = true;
  size_t face = 0;
  const char *field = "none";
};

Difference compare(std::span<const Face> expected, std::span<const Face> actual);

} // namespace spyro::world_recipe
