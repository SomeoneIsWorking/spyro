#include "native_spu_transfer_mode.h"

#include "core.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// The two words the body stores through, each its own `lui $at,0x8007` plus a 16-bit displacement
// at 0x8005CB9C and 0x8005CBA4. The first is the mode itself, read back at 0x8005CBB4; the second
// only ever holds 1 or 0 and is read as a boolean at 0x8005C454.
constexpr std::uint32_t kSpuTransferMode = 0x800730F0u;
constexpr std::uint32_t kSpuModeIsOne = 0x80073570u;
// The high half both `lui $at` instructions leave in $at.
constexpr std::uint32_t kSpuStateSegment = 0x80070000u;

// ── 0x8005CB7C — libspu's SpuSetTransferMode: record the transfer mode a0 asks for, and record
//     separately whether that mode is 1.
// All three arms of the body are delay slots, and that is the whole of it: the `beqz`'s slot loads
// 1 whatever a0 is, the `bne`'s slot clears it again, and the `j`'s slot reloads it. So v0 is 1 for
// mode 1 and 0 for every other mode, mode 0 included, and the word stored second is that same
// value. The stores each rebuild the address from their own `lui`, so $at exits holding 0x80070000,
// the bare high half rather than either store address, and v0 hands back the flag.
void spuSetTransferMode(Core *c) {
  const std::uint32_t mode = c->r[4];
  const std::uint32_t isModeOne = mode == 1u ? 1u : 0u;
  c->mem_w32(kSpuTransferMode, mode);
  c->mem_w32(kSpuModeIsOne, isModeOne);
  c->r[1] = kSpuStateSegment;
  c->r[2] = isModeOne;
}

} // namespace

void registerSpuTransferModeOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x8005CB7Cu, "spu_set_transfer_mode", spuSetTransferMode);
}

} // namespace spyro1::native
