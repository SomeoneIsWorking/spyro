#include "native_audio_key_state.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// The key bit offset arrives masked to 24 bits (`and a1,a1,0xFFFFFF`) and every record below holds
// it as two halfwords: the low 16 at +0, the high 8 at +2.
constexpr std::uint32_t kKeyOffsetMask = 0x00FFFFFFu;
constexpr std::uint32_t kHighHalfShift = 16u;

// Bit 0 of the transfer mode word picks which record pair a call writes, and is the bit the SIO
// path then sets in the pending-update word.
constexpr std::uint32_t kKeyPathBit0 = 1u;

constexpr std::uint32_t kKeyTransferMode = 0x8007354Cu;
constexpr std::uint32_t kKeyUpdatePending = 0x80073118u;
constexpr std::uint32_t kSioKeyBits = 0x80073114u;
constexpr std::uint32_t kCdKeyBits = 0x800730ECu;
constexpr std::uint32_t kCdKeyControl = 0x80073554u;

// The SIO records are one 8-byte block: mode 1 writes the pair at +0, mode 0 the pair at +4, and
// each mode withdraws the bits of the pair the other one wrote.
constexpr std::uint32_t kSioKeyPositionModeOne = 0x800777A8u;
constexpr std::uint32_t kSioKeyPositionModeZero = 0x800777ACu;

// The same two records in the structure the CD path points at, each a low halfword and the high one
// after it: mode 1's pair at +0x188, mode 0's at +0x18C.
constexpr std::uint32_t kCdKeyFieldModeOne = 0x188u;
constexpr std::uint32_t kCdKeyFieldModeZero = 0x18Cu;

// Withdraw the superseded record's bits at one offset: the low halfword is tested against the WHOLE
// offset and the high halfword against only its top byte. v0 exits as the word the high half was
// masked from — the masked word when a bit was set there, and the (zero) mask test when none was —
// and v1 as the inverted top byte, but only where the body builds it: mode 0's arm leaves the
// superseded record's own address there instead, and mode 1's arm leaves the caller's own v1.
void clearSupersededKeyBits(Core *c,
                            std::uint32_t position,
                            std::uint32_t bitOffset,
                            std::uint32_t highHalf) {
  const std::uint32_t low = c->mem_r16(position);
  if ((low & bitOffset) != 0u) {
    c->mem_w16(position, static_cast<std::uint16_t>(low & ~bitOffset));
  }
  const std::uint32_t high = c->mem_r16(position + 2u);
  c->r[2] = high & highHalf;
  if (c->r[2] == 0u) {
    return;
  }
  c->r[3] = ~highHalf;
  c->r[2] &= ~highHalf;
  c->mem_w16(position + 2u, static_cast<std::uint16_t>(c->r[2]));
}

// Both mode arms of one direction: mode 0's at 0x8005C8E4/0x8005C99C and mode 1's at
// 0x8005C7FC/0x8005C8C0. Bit 0 of the transfer mode word picks the SIO records — which also set the
// pending-update flag and keep a 24-bit bits word — over the CD command block, which keeps its own
// bits word and no flag. Each path's bits word is SET on mode 1 and WITHDRAWN on mode 0, so it
// always tracks the bits of the mode-1 record.
//
// Exit state differs per arm and is produced where the retail body produces it. Mode 0's CD arm
// exits with the withdrawn bits word in v0 and the INVERTED offset in v1 (`nor v1,zero,a1`, that
// arm's only v1 write); mode 1's CD arm exits with the merged word in v0 and the command block in
// v1, both words loaded before the two halfword stores.
void applyKeyBit(Core *c, std::uint32_t bitOffset, bool modeZero) {
  const std::uint32_t highHalf = bitOffset >> kHighHalfShift;
  if ((c->mem_r32(kKeyTransferMode) & kKeyPathBit0) != 0u) {
    const std::uint32_t position = modeZero ? kSioKeyPositionModeZero : kSioKeyPositionModeOne;
    c->mem_w16(position, static_cast<std::uint16_t>(bitOffset));
    c->mem_w16(position + 2u, static_cast<std::uint16_t>(highHalf));
    c->mem_w32(kKeyUpdatePending, c->mem_r32(kKeyUpdatePending) | kKeyPathBit0);
    const std::uint32_t superseded = modeZero ? kSioKeyPositionModeOne : kSioKeyPositionModeZero;
    if (modeZero) {
      c->mem_w32(kSioKeyBits, c->mem_r32(kSioKeyBits) & ~bitOffset);
      // v1 is the superseded record's own address here (`lui`+`addiu`, the base both `lhu`s below
      // read through), and it survives to the exit unless the high half is cleared.
      c->r[3] = superseded;
      clearSupersededKeyBits(c, superseded, bitOffset, highHalf);
      return;
    }
    c->mem_w32(kSioKeyBits, c->mem_r32(kSioKeyBits) | bitOffset);
    // v1 is never built on this arm: it exits as the caller's own unless the high half is cleared.
    clearSupersededKeyBits(c, superseded, bitOffset, highHalf);
    return;
  }
  const std::uint32_t control = c->mem_r32(kCdKeyControl);
  const std::uint32_t field = modeZero ? kCdKeyFieldModeZero : kCdKeyFieldModeOne;
  if (modeZero) {
    c->mem_w16(control + field, static_cast<std::uint16_t>(bitOffset));
    c->mem_w16(control + field + 2u, static_cast<std::uint16_t>(highHalf));
    c->r[3] = ~bitOffset;
    c->r[2] = c->mem_r32(kCdKeyBits) & ~bitOffset;
    c->mem_w32(kCdKeyBits, c->r[2]);
    return;
  }
  c->r[2] = c->mem_r32(kCdKeyBits) | bitOffset;
  c->r[3] = control;
  c->mem_w16(control + field, static_cast<std::uint16_t>(bitOffset));
  c->mem_w16(control + field + 2u, static_cast<std::uint16_t>(highHalf));
  c->mem_w32(kCdKeyBits, c->r[2]);
}

// 0x8005C7D4 (libspu's SpuSetKey) — record the CD/XA key bit offset for one direction of the key
// state and withdraw the other direction's bits at that same offset. mode (a0) 0 and 1 write
// opposite records and clear each other's bits; a mode that is neither writes nothing and exits
// with the literal 1 its test built in v0 (`addiu v0,zero,1`, the value the `bne` compares
// against), so that arm hands back the caller's own v1 untouched. The offset is masked to 24 bits
// on entry, and the records read it back zero-extended (`lhu`), which is why the two halves are
// masked apart.
//
// MEASURED on route artisans-walk (23 of 23 sampled calls matching, every RAM byte and compared
// register): the transfer mode word is zeroed by the SPU init block at 0x8005BB60 and no other
// instruction in the executable stores to it, so every live call takes the CD-command arms. The SIO
// pair is therefore UNEXERCISED by the differential, and the early return is unreachable from this
// title's three call sites (0x80055A3C and 0x80056F40 pass mode 0, 0x80056F18 mode 1); both are
// here from the retail bytes rather than folded away.
void setSpuKeyBitPosition(Core *c) {
  const std::uint32_t bitOffset = c->r[5] & kKeyOffsetMask;
  const std::uint32_t mode = c->r[4];
  if (mode == 0u) {
    applyKeyBit(c, bitOffset, true);
    return;
  }
  if (mode != 1u) {
    c->r[2] = 1u;
    return;
  }
  applyKeyBit(c, bitOffset, false);
}

} // namespace

void registerAudioKeyStateOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x8005C7D4u, "set_spu_key_bit_position", setSpuKeyBitPosition);
}

} // namespace spyro1::native
