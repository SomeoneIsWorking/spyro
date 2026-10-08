// The engine's angle helper and its calibrated spin, owned outright as leaves: a leaf's whole
// effect is registers plus memory, which is exactly what the per-call differential compares.
//
// Every register a body leaves is part of the contract, including scratch registers no caller can
// legitimately read, and delay slots decide the exit state: the instruction after the branch runs
// either way, so porting it as part of the taken path matches one exit and diverges the other.
#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

// 0x80017908 — shortest absolute separation of two 8-bit angles.
//     sub a0,a0,a1 ; andi a0,a0,0xFF ; addi a1,a0,-128 ; bltz a1,L ; addi a1,zero,256
//     sub a0,a1,a0 ; L: jr ra ; addi v0,a0,0
//
// a1 exits as 256 on both paths: `addi a1,zero,256` is the delay slot of the bltz, so it is not the
// "d >= 128" arm of a conditional, even though it reads like one.
void angdiff8_native(Core *c) {
  const int32_t d = (int32_t)((c->r[4] - c->r[5]) & 0xFFu);
  const int32_t v = (d - 128 < 0) ? d : 256 - d;
  c->r[4] = (uint32_t)v;
  c->r[5] = 256u;
  c->r[2] = (uint32_t)v;
}

// 0x8001796C — the 12-bit signed angle wrap — is deliberately not owned: its 11 static callers sit
// on gameplay paths no headless run can drive yet.

// 0x8005C720 — a calibrated busy-wait: 60 iterations of v *= 13 whose product no one reads, purely
// to burn a fixed number of cycles. The replacement stays byte-exact because the differential
// compares all of RAM, and the two words below `$sp` at exit are dead stack the guest still wrote.
//
// v is loaded at the top of the body, so it lags the stored product by one multiply: at exit v1
// holds 13^60 while the stack word is 13^61.
void spin60_native(Core *c) {
  uint32_t v = 13u, loaded = 13u;
  for (int i = 0; i < 60; i++) {
    loaded = v;
    v *= 13u;
  }
  const uint32_t sp = c->r[29] - 8u; // the frame the body allocates and then pops
  c->mem_w32(sp + 4, v);
  c->mem_w32(sp + 0, 60u);
  c->r[2] = 0u; // the slti that failed the loop test
  c->r[3] = loaded;
}

} // namespace

void spyro::registerNativeAngle(Core &core) {
  psx::cpu::installNativeOverride(core, 0x80017908u, "angdiff8", angdiff8_native);
  psx::cpu::installNativeOverride(core, 0x8005C720u, "spin60", spin60_native);
}
