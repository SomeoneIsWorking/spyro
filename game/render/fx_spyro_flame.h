#pragma once

struct Core;

// Direct native owner of Spyro's flame renderer 0x80058D64. It reads its orientation out of
// g_SpyroFlame+0xB8, which the Spyro producer publishes (see spyro_flame_matrix.h); without that
// publication every flame point projects onto the flame origin and the ribbon collapses into one
// pixel, so this producer must stay ordered after 0x80023AC4. 0x80019698 calls it only while
// g_SpyroFlame.m_IsFlameActive is set, so an inactive flame is a valid empty frame rather than a
// refusal. Returns false only when the producer cannot represent the live state.
bool spyro_flame_submit(Core *core);
