#pragma once

#include "world_hq_recipe.h"
#include "world_scene_oracle.h"

#include <cstdint>
#include <span>
#include <vector>

class Core;

namespace spyro::world_scene_capture {

enum class Status : uint8_t { Match, Mismatch, Refused };

enum class Phase : uint8_t {
  LowDirect,
  HighDirect,
  MediumTransition,
  MediumCore,
  NearTransition,
  NearCore,
};

// Diagnostic-only lifecycle. begin() arms only when
// PSXPORT_WORLD_SCENE_ORACLE=1; every injected hook is otherwise a no-op.
void begin(Core *core);
void noteHighFace(Core *core,
                  uint32_t source,
                  uint32_t v0,
                  uint32_t v1,
                  uint32_t v2,
                  uint32_t v3,
                  uint32_t status);
void noteCoarseHighProjection(Core *core, uint32_t sector, uint32_t cacheAddress);
void noteLink(Core *core, uint32_t packet, uint32_t otSlot, Phase phase, uint32_t source = 0);
void noteAdaptiveChild(Core *core, uint32_t packet, uint32_t original);
void noteAdaptiveDeferred(Core *core, uint32_t packet, uint32_t original);
void noteAdaptiveReplacement(Core *core, uint32_t original, uint32_t head, uint32_t otSlot);
void finishGuest(Core *core, uint32_t poolEnd);

// Moves the most recent complete capture out. Returns false for disabled,
// incomplete, or malformed captures.
bool take(Core *core, std::vector<world_scene_oracle::Record> &records);

// Consumes the completed retail capture and compares it with a semantic
// recipe. This is diagnostic only and never submits either stream.
Status verify(Core *core,
              std::span<const world_recipe::Face> semantic,
              std::span<const world_hq_recipe::AuditEntry> audit = {});

} // namespace spyro::world_scene_capture
