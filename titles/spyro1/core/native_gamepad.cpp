#include "native_gamepad.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>
#include <string_view>

namespace spyro1::native {

namespace {

constexpr std::string_view kName = "copy_pad_to_destination_and_reset_source";
// g_PadSwapFlag (gamepad.h): both the `lui 0x8007` load and the `lui 0x8007` store form 0x5944, so
// the flag this body toggles is one byte at 0x80075944.
constexpr std::uint32_t kPadSwapFlag = 0x80075944u;
constexpr std::uint32_t kGamepadBytes = 0xA4u;
// Memcpy(dst, src, len), the `jal` at 0x80053738. Already owned natively as the `copyw` override
// at 0x80016958, so this call lands in that owner rather than in the JIT.
constexpr std::uint32_t kMemcpy = 0x80016958u;
// PadReset(pad), the `jal` at 0x80053740 in gamepad.c's pad-buffer swap. No native owner: it runs
// through Lightrec.
constexpr std::uint32_t kPadReset = 0x800536A4u;

// ── 0x80053708 — move the whole 0xA4-byte Gamepad from a0 to a1, clear the buffer it came from,
//     and flip g_PadSwapFlag so the copy is the live buffer. The 32-bit difference `1 - flag`
//     reaches the store as a BYTE, and each `jal` takes the argument it needs from the delay slot
//     beneath it (a2 = 0xA4 for the copy, a0 = the source for the reset). Nothing of v0 or v1
//     survives the second call — the registers this body exits with are PadReset's — so both
//     callees are called through the dispatcher rather than inlined here.
void copyPadToDestinationAndResetSource(Core *c) {
  const std::uint32_t source = c->r[4];
  const std::uint32_t destination = c->r[5];
  const std::uint32_t swapped = 1u - static_cast<std::uint32_t>(c->mem_r8(kPadSwapFlag));
  c->r[2] = swapped;
  c->mem_w8(kPadSwapFlag, static_cast<std::uint8_t>(swapped));
  psx::cpu::callGuestNow(*c, kName, kMemcpy, destination, source, kGamepadBytes);
  psx::cpu::callGuestNow(*c, kName, kPadReset, source);
}

} // namespace

void registerGamepadOverrides(Core &core) {
  spyro::installNativeOverride(core,
                               0x80053708u,
                               "copy_pad_to_destination_and_reset_source",
                               copyPadToDestinationAndResetSource);
}

} // namespace spyro1::native
