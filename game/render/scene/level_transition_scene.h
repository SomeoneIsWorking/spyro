#pragma once

// level_transition_scene — func_8001A050, the producer for stage 1 (GS_LevelTransition).
//
// Its own piece is the level-entrance sweep: a residual angle it winds down by 2 a frame, and while
// that runs the sky uses a substitute view/projection pair rather than the camera's matrices.

#include <array>
#include <cstdint>

class Core;

namespace spyro::level_transition_scene {

// Why a frame refused, so the caller can name it rather than reporting a bare false.
enum class Refusal : std::uint8_t {
  None,
  Tally,
  ShadedActors,
  SpyroActor,
  Sky,
};

// The substitute sky matrices for a sweep residual, as the guest builds them.
struct SkyMatrices {
  std::array<std::uint32_t, 5> view{};
  std::array<std::uint32_t, 5> projection{};
};

SkyMatrices sweepSkyMatrices(Core *core, std::int32_t residual);

// Wind the entrance sweep down by one frame and report what it is now. The guest does this inside
// the producer, so it is state this owner keeps rather than something the caller supplies.
std::int32_t advanceEntranceSweep(Core *core);

Refusal submit(Core *core);

const char *refusalName(Refusal refusal);

} // namespace spyro::level_transition_scene
