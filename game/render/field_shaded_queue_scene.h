#pragma once

#include "field_shaded_queue_recipe.h"

#include <cstdint>
#include <vector>

class Core;

namespace spyro::field_shaded_queue_scene {

enum class Status : uint8_t {
  Ready,
  InvalidQueue,
  UnterminatedQueue,
  InvalidActor,
  InvalidMesh,
  InvalidShadowCursor,
  UnsupportedTransformScale,
  UnsupportedVertexLighting,
};

struct Shadow {
  uint32_t actor = 0;
  uint32_t modelByte = 0;
};

struct Frame {
  field_shaded_queue_recipe::Input input{};
  std::vector<uint32_t> visitedWorldActors;
  std::vector<uint32_t> transformedActors;
  std::vector<Shadow> shadows;
  uint32_t shadowCursor = 0;
  uint32_t queueRecords = 0;
  uint32_t screenRecords = 0;
  uint32_t validMeshRecords = 0;
  uint32_t validMeshPrimitiveCandidates = 0;
  std::vector<uint16_t> sourceMeshIndices;
  std::vector<int32_t> sourceLightingOffsets;
  uint32_t nullMeshes = 0;
  uint32_t culled = 0;
  uint32_t primitiveCandidates = 0;
};

Status prepare(Core *core, int32_t clipRight, Frame &frame);
void commit(Core *core, const Frame &frame);
const char *statusName(Status status);

} // namespace spyro::field_shaded_queue_scene
