// frame_env.h — the native leg's frame open/close. See frame_env.cpp for the RE this is ported
// from.
#pragma once
#include <cstdint>
class Core;

// Spyro 1's measured logic cadence: the guest's own frame tail (frame_env.cpp's RE of
// FUN_8007cee4/FUN_8001ed5c) spends at least TWO display fields per drawn logic frame — 30 Hz
// logic on the 60 Hz display. Whatever paces the logic frame must represent the same two fields:
// pacing one field per logic frame runs the whole game at twice its retail speed (user-reported
// 2026-08-28: run.sh "runs unbounded speed", measured 115 vblank fields/s = ~57 logic fps).
inline constexpr int kFieldsPerLogicFrame = 2;

// Flip the game's draw env (0x8001ED5C's own rule) and program the GPU from it: draw area, draw
// offset, tpage, texture window, and the background fill when the env asks for one. Returns the env
// now being drawn with — hand it back to nativeFrameEnd.
uint32_t nativeFrameBegin(Core *c);

// Pure buffer policy: normal preserves the guest's previous-buffer DISPENV; an FPS60 commit selects
// the reciprocal DISPENV whose start names the just-drawn current buffer. Returns zero for an
// unknown environment so diagnostics cannot silently certify a guessed address.
uint32_t nativeFrameDisplayEnv(uint32_t drawEnv, bool fps60CommitPending);

// Spend the guest fields, then select the display start dictated by nativeFrameDisplayEnv().
void nativeFrameEnd(Core *c, uint32_t env, bool fps60CommitPending = false);
