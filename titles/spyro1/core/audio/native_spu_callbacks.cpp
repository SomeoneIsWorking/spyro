#include "native_spu_callbacks.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// 0x8005DE28 — the library's callback dispatcher, the target of the `jal` at 0x8005C794. It reads
// the hook entry out of the table at D_8007498C and calls it with the class and the caller's
// pointer.
constexpr std::uint32_t kCallbackDispatch = 0x8005DE28u;
constexpr char kCallbackOwner[] = "register_spu_irq_callback";
// The class the body names, not a count: it is the index the dispatcher installs the pointer under.
constexpr std::uint32_t kSpuIrqCallbackClass = 4u;
// The body's own frame, and the slot inside it that holds $ra.
constexpr std::uint32_t kFrameBytes = 0x18u;
constexpr std::uint32_t kReturnSlotOffset = 0x10u;

// 0x8005C788 — hand the caller's routine pointer to the library's callback dispatcher as class 4.
// The one caller is the SPU init at 0x8005BB78, and what it hands over is the guest's SPU IRQ
// routine at 0x8005C054, which clears the SPU's busy bits, spins until the transfer drains and
// restarts SPU DMA channel 9. The class is built in the `jal`'s delay slot, so $a0 is already 4
// when the callee runs and the pointer survives only in $a1.
//
// The 0x18-byte frame has to be reproduced even though nothing in it is read afterwards: the callee
// allocates the same 0x18 bytes BELOW this one and stores its $ra at frame-0x08, so with $sp left
// unlowered that store lands in the CALLER's live frame (the SPU init's own saved $ra) instead of
// in dead stack. The epilogue then reads this frame's slot back, and it still holds the $ra the
// body was called on, because the callee never reaches it — so $ra and $sp exit exactly as they
// came in and only the dead word below $sp differs. $v0 and $v1 belong to the callee: it ends in
// 0x8005E804, which returns the hook table's previous entry in $v0, so nothing here assigns them.
void registerSpuIrqCallback(Core *c) {
  const std::uint32_t frame = c->r[29] - kFrameBytes;
  c->r[29] = frame;
  c->mem_w32(frame + kReturnSlotOffset, c->r[31]);
  psx::cpu::callGuestNow(*c, kCallbackOwner, kCallbackDispatch, kSpuIrqCallbackClass, c->r[4]);
  c->r[29] = frame + kFrameBytes;
}

} // namespace

void registerSpuCallbackOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8005C788u, "register_spu_irq_callback", registerSpuIrqCallback);
}

} // namespace spyro1::native
