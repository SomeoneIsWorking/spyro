#include "native_spu_registers.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// D_80073554: the base the guest indexes the SPU register file from. The retail data image
// initialises it to 0x1F801C00 — voice 0's left volume — and the library never rewrites it, so the
// halfword index a caller passes selects a register out of the I/O map rather than out of main RAM.
constexpr std::uint32_t kSpuRegisterFile = 0x80073554u;
// D_8007357C: the register-field scale, 3 in the shipped data image. The library takes an 8-bit
// guest quantity and stores the 5-bit field the hardware register holds, and its reader
// (0x8005C62C) scales back the other way with `sllv`, so this word is the only scale the body
// needs.
constexpr std::uint32_t kSpuRegisterFieldShift = 0x8007357Cu;
// `srlv` uses the low five bits of its shift register and ignores the rest.
constexpr std::uint32_t kShiftCountMask = 0x1Fu;

// ── 0x8005C540 — store one halfword of the SPU register file at a halfword index. a2 selects the
//     form: zero stores a1 whole, nonzero stores it shifted right by D_8007357C first.
// The index is scaled in the delay slot of the selecting branch, so BOTH arms index with the a0 the
// caller passed; the shifted arm then reuses $a0 to reload the base, and that reload is not what
// offsets the index. v0 therefore exits as the resolved register address either way.
//
// v1 is not the stored value on both arms, which is the one thing a source-level reading gets
// wrong: the direct arm exits with the base it loaded, and only the shifted arm exits with the
// value it stored. The store is the last instruction of whichever arm ran.
void spuWriteRegisterHalfword(Core *c) {
  const std::uint32_t index = c->r[4];
  const std::uint32_t value = c->r[5];
  const bool scaleToField = c->r[6] != 0u;
  const std::uint32_t base = c->mem_r32(kSpuRegisterFile);
  const std::uint32_t address = base + (index << 1);
  std::uint32_t stored = value;
  std::uint32_t exitV1 = base;
  if (scaleToField) {
    stored = value >> (c->mem_r32(kSpuRegisterFieldShift) & kShiftCountMask);
    exitV1 = stored;
  }
  c->r[2] = address;
  c->r[3] = exitV1;
  c->mem_w16(address, static_cast<std::uint16_t>(stored));
}

} // namespace

void registerSpuRegisterOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8005C540u, "spu_write_register_halfword", spuWriteRegisterHalfword);
}

} // namespace spyro1::native
