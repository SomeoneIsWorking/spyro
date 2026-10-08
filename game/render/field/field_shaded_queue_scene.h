#pragma once

#include "field_shaded_queue_recipe.h"
#include "moby_shadow_list.h"

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
};

struct Shadow {
  uint32_t actor = 0;
  uint32_t modelByte = 0;
};

struct Frame {
  // Drawn half: the records and shadow entries under the widened plane (issue 0152).
  field_shaded_queue_recipe::Input input{};
  moby_shadow_list::List drawnShadows;
  // Guest half: every visited actor has +0x51 cleared, those retail's 512-px planes transform have
  // it set, and the shadow list holds retail's entries.
  std::vector<uint32_t> visitedWorldActors;
  std::vector<uint32_t> transformedActors;
  std::vector<Shadow> shadows;
  uint32_t shadowCursor = 0;
  uint32_t queueRecords = 0;
  // Mobies whose render-radius byte (+0x50) has bit 7 set: the screen-space population
  // (`g_Hud` Mobys and HUD glyphs), drawn by retail's own screen-space path and never culled.
  uint32_t screenSpaceActors = 0;
  uint32_t validMeshRecords = 0;
  uint32_t validMeshPrimitiveCandidates = 0;
  std::vector<uint16_t> sourceMeshIndices;
  std::vector<int32_t> sourceLightingOffsets;
  uint32_t nullMeshes = 0;
  uint32_t culled = 0;
  uint32_t primitiveCandidates = 0;
};

// `clipRight` is the drawn width: it widens the horizontal plane and the projection window of what
// the port draws, never the guest half.
Status prepare(Core *core, int32_t clipRight, Frame &frame);
void commit(Core *core, const Frame &frame);
const char *statusName(Status status);

} // namespace spyro::field_shaded_queue_scene
