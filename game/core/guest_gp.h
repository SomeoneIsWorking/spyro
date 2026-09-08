// guest_gp.h — Spyro's $gp, and the rule every gp-relative address in this port is written by.
//
// crt0 loads gp at 0x8005B95C: `lui gp,0x8007 ; addiu gp,gp,0x5264` -> 0x80075264. game_config.cpp
// records the same value from the same instruction pair, so this is not a second source of truth
// for it — it is that value made reachable from code that has no GameConfig in scope.
//
// WHY A HEADER. Every gp-relative global the MIPS code touches is `kGp + <the signed 16-bit
// displacement in the instruction it came from>`; writing it that way keeps each constant checkable
// against a listing instead of being a bare hex address nobody can re-derive. Two modules need the
// base — Spyro1FrameDriver and the scene classifier — and a literal copied into each is a second
// place to get it wrong.
#pragma once
#include <cstdint>

constexpr uint32_t kGp = 0x80075264u;
