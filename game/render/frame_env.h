// frame_env.h — the native leg's frame open/close. See frame_env.cpp for the RE this is ported from.
#pragma once
#include <cstdint>
class Core;

// Flip the game's draw env (0x8001ED5C's own rule) and program the GPU from it: draw area, draw
// offset, tpage, texture window, and the background fill when the env asks for one. Returns the env
// now being drawn with — hand it back to nativeFrameEnd.
uint32_t nativeFrameBegin(Core* c);

// Set the display start from that env's DISPENV — the guest's own PutDispEnv(env + 0x5C).
void nativeFrameEnd(Core* c, uint32_t env);
