// frame_env.h — the native leg's frame open/close. See frame_env.cpp for the RE this is ported
// from.
#pragma once
#include <cstdint>
class Core;

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
