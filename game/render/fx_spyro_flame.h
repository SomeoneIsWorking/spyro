#pragma once

struct Core;

// Direct native owner of Spyro's flame renderer 0x80058D64. NOT wired into the FIELD composition
// yet: 0x80023AC4 publishes its live GTE rotation matrix into g_SpyroFlame+0xB8 (at 0x80024110,
// gated on g_SpyroFlame+0x9A), and the port's native Spyro producer replaced that routine without
// carrying the publication over. Measured on a live flame: the five words are zero, so every flame
// point projects onto the flame origin and the whole ribbon collapses into one pixel. Wiring this
// in before the matrix is published would draw 140 degenerate quads per frame and read as a
// working flame.
// 0x80019698 calls it only while
// g_SpyroFlame.m_IsFlameActive is set, so an inactive flame is a valid empty frame rather than a
// refusal. Returns false only when the producer cannot represent the live state.
bool spyro_flame_submit(Core *core);
