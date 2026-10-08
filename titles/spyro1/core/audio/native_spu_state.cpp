#include "native_spu_state.h"

#include "core.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// The pointer the SCE library keeps to its own state structure. The body builds the address itself
// — `lui $a0,0x8007` then `lw $a0,0x3568($a0)` — so the address audit re-derives this
// constant from those two instructions and refuses the module if it is not what retail computes.
constexpr std::uint32_t kSpuStatePointer = 0x80073568u;
// The word is rebuilt from two masks, so each is a constant of its own: the `lui $v1,0xF0FF` plus
// the `ori $v1,$v1,0xFFFF` is one mask written across two instructions, and the `lui $v1,0x2000`
// is the other. Neither is a guest address, so neither is checked against the image.
constexpr std::uint32_t kPreservedStateBits = 0xF0FFFFFFu;
constexpr std::uint32_t kForcedStateBit = 0x20000000u;

// ── 0x8005C6C8 — force one control word of the SCE sound library's own state.
//     a0 = [0x80073568] ; v0 = [a0] ; v0 = (v0 & 0xF0FFFF) | 0x20000000 ; [a0] = v0 ; jr ra
// The word sits behind a pointer the library holds in initialised data, and the entry takes no
// argument: a0 is scratch for the load and exits holding the pointer, so nothing the caller passed
// is read. Three fields of the word are rewritten at once — both low bytes and bits 24..27 are
// cleared and bit 29 is forced — which is why the name is a word and not a bit. The forced bit is
// `lui $v1,0x2000`, so it is 0x2000 shifted into the high half: 0x20000000, bit 29, and not the
// 0x00200000 that reads alike. v1 exits holding that immediate rather than a mask, and v0 carries
// the stored word; neither is reachable by the caller, and both are part of the contract this body
// keeps.
void forceSpuStateControlWord(Core *c) {
  const std::uint32_t state = c->mem_r32(kSpuStatePointer);
  c->r[4] = state;
  c->r[2] = c->mem_r32(state);
  c->r[3] = kPreservedStateBits;
  c->r[2] &= c->r[3];
  c->r[3] = kForcedStateBit;
  c->r[2] |= c->r[3];
  c->mem_w32(state, c->r[2]);
}

} // namespace

void registerSpuStateOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8005C6C8u, "spu_force_state_control_word", forceSpuStateControlWord);
}

} // namespace spyro1::native
