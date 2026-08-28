#pragma once

#include "actor_recipe_capture.h"
#include "actor_scene_builder.h"

#include <cstdint>
#include <vector>

class Core;

namespace spyro::secondary_actor_scene {

enum class Status : uint8_t {
  Ready,
  InvalidSourceList,
  UnterminatedSourceList,
  RecordCapacityExceeded,
  RecordCaptureRefused,
  CulledShadowSideEffectUnowned,
  InvalidShadowCursor,
};

struct Shadow {
  uint32_t moby = 0;
  uint32_t modelByte = 0;
};

struct Record {
  uint32_t moby = 0;
  uint32_t lightingControl = 0;
  actor_recipe_capture::Record actor{};
};

struct Frame {
  std::vector<uint32_t> visitedMobys;
  std::vector<Record> records;
  std::vector<Shadow> shadows;
  actor_scene::Census census{};
  uint32_t shadowCursor = 0;
};

// Native semantic replacement for 0x800208FC. It consumes the negative-state
// list authored by 0x800521C0 and deep-copies complete secondary-render records
// without materialising the renderer's temporary 0x800712F4 arena.
Status prepare(Core *core, Frame &frame);

// Publish only the retail game-state side effects after the renderer has
// preflighted the whole call: actor transform-state bytes and Moby shadows.
void commit(Core *core, const Frame &frame);

const char *status_name(Status status);

} // namespace spyro::secondary_actor_scene
