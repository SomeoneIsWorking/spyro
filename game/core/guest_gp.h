// crt0 loads gp at 0x8005B95C: `lui gp,0x8007 ; addiu gp,gp,0x5264` -> 0x80075264.
//
// gp-relative globals are written `kGp + <the signed 16-bit displacement in the instruction it came
// from>`, which keeps each constant checkable against a listing.
#pragma once
#include <cstdint>

constexpr uint32_t kGp = 0x80075264u;
