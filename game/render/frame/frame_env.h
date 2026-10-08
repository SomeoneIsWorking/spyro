// The native leg's frame open/close.
#pragma once
#include "spyro1_frame_policy.h"
#include <cstdint>
class Core;

namespace spyro::render {

// The guest's frame tail spends at least two display fields per drawn logic frame: 30 Hz logic on a
// 60 Hz display. Pacing one field per logic frame runs the whole game at twice its retail speed.
inline constexpr int kFieldsPerLogicFrame = (int)spyro1::kFieldsPerLogicFrame;

// Returns the env now being drawn with — hand it back to frameEnd.
std::uint32_t frameBegin(Core *core);

// Pure buffer policy: normal preserves the guest's previous-buffer DISPENV; an FPS60 commit selects
// the reciprocal DISPENV whose start names the just-drawn current buffer. Returns zero for an
// unknown environment.
std::uint32_t frameDisplayEnv(std::uint32_t drawEnv, bool fps60CommitPending);

// Spend the guest fields, then select the display start dictated by frameDisplayEnv().
void frameEnd(Core *core, std::uint32_t env, bool fps60CommitPending = false);

} // namespace spyro::render
