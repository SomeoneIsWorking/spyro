// Reading the terrain producer's corpus out of guest memory (0x8004EBA8).
//
// WHY THE READS ARE THEIR OWN OWNER. Everything this returns is a snapshot of one game update: the
// object list the guest resolved, each surviving object's decoded model vertices, its face table
// and the colour words those faces name. Once it is a value, the same corpus can be projected again
// between game updates, when guest memory already describes the NEXT update and cannot be read.
//
// WHAT IT DELIBERATELY DOES NOT REFUSE. Retail bounds-checks an object's face table and a face's
// colour word only after that object and face have survived the clip tests, which need projection.
// A capture that refused on either would refuse on objects and faces retail never looked at, so
// those two conditions are carried into the corpus as flags for `terrain_recipe` to act on at the
// point it reaches them.
#pragma once

#include "terrain_recipe.h"

#include <array>
#include <cstdint>
#include <optional>

class Core;

namespace spyro::terrain_scene {

enum class Status : uint8_t {
  Ready,
  Refused,
};

struct Capture {
  Status status = Status::Refused;
  const char *refusal = "none";
  terrain_recipe::Input input;
};

// The five packed words of a guest SHORTMATRIX, or nothing when the pointer is outside RAM. The
// producer itself never needs the address again, so this is the one boundary with an address to
// bounds-check.
std::optional<std::array<uint32_t, 5>> matrixWords(Core &core, uint32_t address);

// `selector` below zero walks the whole terrain object list; otherwise it indexes the guest's table
// of per-region index lists. `cullWords` decides which objects are visible at all and `viewWords`
// transforms the vertices of those that are; both carry a zero translation.
Capture capture(Core &core,
                int32_t selector,
                const std::array<uint32_t, 5> &cullWords,
                const std::array<uint32_t, 5> &viewWords);

} // namespace spyro::terrain_scene
