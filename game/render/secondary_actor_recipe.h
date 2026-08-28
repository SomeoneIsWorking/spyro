#pragma once

#include "actor_draw_recipe.h"
#include "secondary_actor_scene.h"

#include <cstdint>
#include <vector>

namespace spyro::secondary_actor_recipe {

enum class Status : uint8_t { Ready, ValidEmpty, UnsupportedPrefix, UnsupportedLighting };

struct Recipe {
  Status status = Status::ValidEmpty;
  actor_draw_recipe::Reason firstReason = actor_draw_recipe::Reason::None;
  uint32_t sourceRecords = 0;
  uint32_t candidates = 0;
  uint32_t rejectedCandidates = 0;
  uint32_t firstUnsupportedRecord = 0;
  uint32_t firstUnsupportedSourceWord = 0;
  std::vector<actor_prefix::Output> outputs;
  std::vector<actor_draw_recipe::Face> faces;
};

// Pure 0x80020F34 preflight. The renderer shares the Moby compressed-model
// projection and ordinary G3/G4/GT3/GT4 topology with 0x8001F798, but control
// bit 2 selects its separate per-face specular-lighting program. Unsupported
// input clears every face so a partial secondary actor cannot be presented.
Recipe derive(const secondary_actor_scene::Frame &frame);

} // namespace spyro::secondary_actor_recipe
