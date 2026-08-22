#pragma once

#include "world_recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::world_scene_oracle {

// A final retail-packet-observable world primitive. Builder-only provenance
// (sector/source addresses) and view depth are deliberately absent: PSX GPU
// packets carry neither, so claiming this stream independently proves those
// fields would be false. Depth is checked at the pure projection seam instead.
struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  uint32_t rgb = 0;
  uint8_t u = 0;
  uint8_t v = 0;
};

struct Record {
  world_recipe::Family family = world_recipe::Family::G3;
  uint8_t vertexCount = 3;
  uint16_t otBin = 0;
  uint32_t packet = 0;
  uint32_t source = 0;                 // diagnostic provenance; excluded from equality
  std::array<uint32_t, 4> clipWords{}; // diagnostic provenance; excluded from equality
  uint32_t clipStatus = 0;             // diagnostic provenance; excluded from equality
  std::array<Vertex, 4> vertices{};
  world_recipe::Material material{};
};

struct Difference {
  bool equal = true;
  size_t record = 0;
  const char *field = "none";
};

// Converts the semantic producer's authored records to the retail OT's final
// draw order. Returns false when paint identity is missing or ambiguous.
bool expected(std::span<const world_recipe::Face> faces, std::vector<Record> &records);

Difference compare(std::span<const Record> retail, std::span<const Record> semantic);

// Positive equality followed by a one-field corruption. A green result proves
// the comparator can report the other answer instead of accepting everything.
bool corruptionSelftest();

} // namespace spyro::world_scene_oracle
