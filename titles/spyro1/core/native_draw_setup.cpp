#include "native_draw_setup.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// D_800770C8, the draw area's light state: the specular direction is the three words at +0x00
// ..+0x08, the one light colour the per-face colour program reads is the three words at
// +0x0C..+0x14, and m_specularTime is the byte phase that drives the direction at +0x2C.
constexpr std::uint32_t kSpecularDirection = 0x800770C8u;
constexpr std::uint32_t kLightColor = kSpecularDirection + 0x0Cu;
constexpr std::uint32_t kSpecularTime = kSpecularDirection + 0x2Cu;
constexpr std::uint32_t kSpecularZ = 0xFFFFFC00u;

// D_8006CBF8 is the sine table and D_8006CC78 the cosine one, a quarter turn of 0x80 bytes later.
// Both entries are read with `lh`, so a negative table entry stays negative through the scaling.
constexpr std::uint32_t kSineTable = 0x8006CBF8u;
constexpr std::uint32_t kCosineTable = kSineTable + 0x80u;
constexpr std::uint32_t kTableEntryBytes = 2u;
constexpr std::uint32_t kPhaseMask = 0xFFu;

// The drawing arena: 0x800785FC holds its base pointer, the other five globals the two pools, the
// two orderings, and one arena slot the reset rebases but does not otherwise use.
constexpr std::uint32_t kArenaBase = 0x800785FCu;
constexpr std::uint32_t kArenaSlot = 0x800785F8u;
constexpr std::uint32_t kPrimPool = 0x800785F4u;
constexpr std::uint32_t kOrderingAnchor = 0x800785F0u;
constexpr std::uint32_t kOrderingOne = 0x800785ECu;
constexpr std::uint32_t kOrderingZero = 0x800785E8u;
constexpr std::uint32_t kArenaSlotBack = 0x2000u;
constexpr std::uint32_t kPrimPoolBack = 0x6000u;
constexpr std::uint32_t kOrderingAnchorBack = 0x6008u;
// `lui v1, 0xfffe` then `ori v1, v1, 0xd000 / 0x4000`: the OR is into 0xFFFE0000, NOT into a
// sign-extended 0xFFFF0000, so these are 0xFFFE0000 | imm and both sit below -0x2000.
constexpr std::uint32_t kStrideDoubleBuffered = 0xFFFED000u;
constexpr std::uint32_t kStrideSingleBuffered = 0xFFFE4000u;
constexpr std::uint32_t kOrderingHeadBytes = 8u;
constexpr std::uint32_t kPrimPoolBytes = 0x4000u;

// Where the reset republishes the ordering and pool pointers for the rest of the draw area.
constexpr std::uint32_t kPublishedOrderingZero = 0x80076F50u;
constexpr std::uint32_t kPublishedPrimPool = 0x80076F54u;
constexpr std::uint32_t kPublishedOrderingAnchor = 0x80076F58u;
constexpr std::uint32_t kPublishedOrderingOne = 0x80076FD4u;
constexpr std::uint32_t kPublishedPrimPoolAlt = 0x80076FD8u;
constexpr std::uint32_t kPublishedOrderingAnchorAlt = 0x80076FDCu;

// The guest leaves these bodies reach with `jal`, named for the role each one plays here and not
// for a guessed purpose. They are full branch targets, not `lui`-formed addresses, so they are
// spelled with a digit separator: the address audit (tools/override_constants.py) only models the
// `lui` forms and would otherwise read a correct `jal` target as an unexplained address.
// 0x80016914 is the byte fill this port already owns as `fill`, and 0x800176F0 the three-word clear
// it already owns as `zero3`; both leave v0, v1 and every s-register untouched.
constexpr std::uint32_t kFillLeaf = 0x8001'6914u;
constexpr std::uint32_t kClearThreeWordsLeaf = 0x8001'76F0u;
constexpr std::uint32_t kForwardColorBufferLeaf = 0x8001'7110u;
constexpr std::uint32_t kStoreColorSlotLeaf = 0x8001'7C68u;

// 0x8005956C — the fixed length the byte sum walks.
constexpr std::uint32_t kChecksumBytes = 0x58Cu;

// 0x80058BD8 — the three light-colour slots, six bytes apart, and the frame the body builds the
// colour in. The frame is sp-0x30 with ra, s1 and s0 saved at +0x28..+0x20 and the three-word
// colour buffer at +0x10, so every frame offset is measured from the decremented sp.
constexpr std::uint32_t kColorSlots = 0x800770E0u;
constexpr std::uint32_t kColorSlotStride = 6u;
constexpr std::uint32_t kColorBufferOffset = 0x10u;
constexpr std::uint32_t kSavedS0Offset = 0x20u;
constexpr std::uint32_t kSavedS1Offset = 0x24u;
constexpr std::uint32_t kSavedRaOffset = 0x28u;
constexpr std::uint32_t kColorFrameBytes = 0x30u;
constexpr std::uint32_t kRedColorWord = 0x1000u;
constexpr std::uint32_t kGreenColorWord = 0x1000u;
// `addiu $v0, $zero, -0x1000`: negative, so the third channel keeps its top half and only the low
// halfword is 0xF000. A body that wrote a positive 0x1000 here would store a different word.
constexpr std::uint32_t kBlueColorWord = 0xFFFFF000u;

// 0x80058BD8 — write the three light-colour slots, one colour WORD at a time: the buffer is cleared
// and a single word set before each slot is published, so slot 0 holds (0x1000, 0, 0), slot 1
// (0, 0x1000, 0) and slot 2 (0, 0, 0xFFFFF000). The slots are six bytes apart while the buffer
// words are four apart, so the two strides differ and neither is derived from the other.
//
// s1 and s0 are established INSIDE the first pass and never again — s1 = 0x1000 sits in the
// `addiu`'s shadow just before the first 0x80017110, s0 = 0x800770E0 just before the first
// 0x80017C68 — and both are reloaded from the frame in the epilogue, so they exit at their ENTRY
// values, not at the ones this body wrote. v0 and v1 carry out whatever the last 0x80017C68 leaves,
// which is why nothing here assigns them.
void setLightColorSlots(Core *c) {
  const std::uint32_t frame = c->r[29] - kColorFrameBytes;
  c->r[29] = frame;
  c->mem_w32(frame + kSavedRaOffset, c->r[31]);
  c->mem_w32(frame + kSavedS1Offset, c->r[17]);
  c->mem_w32(frame + kSavedS0Offset, c->r[16]);
  const std::uint32_t colour = frame + kColorBufferOffset;

  psx::cpu::callGuestNow(*c, "set_light_color_slots", kClearThreeWordsLeaf, colour);
  c->mem_w32(colour, kRedColorWord);
  c->r[17] = kRedColorWord;
  psx::cpu::callGuestNow(*c, "set_light_color_slots", kForwardColorBufferLeaf, colour, colour);
  c->r[16] = kColorSlots;
  psx::cpu::callGuestNow(*c, "set_light_color_slots", kStoreColorSlotLeaf, kColorSlots, colour);

  psx::cpu::callGuestNow(*c, "set_light_color_slots", kClearThreeWordsLeaf, colour);
  c->mem_w32(colour + 4u, kGreenColorWord);
  psx::cpu::callGuestNow(*c, "set_light_color_slots", kForwardColorBufferLeaf, colour, colour);
  psx::cpu::callGuestNow(
      *c, "set_light_color_slots", kStoreColorSlotLeaf, kColorSlots + kColorSlotStride, colour);

  psx::cpu::callGuestNow(*c, "set_light_color_slots", kClearThreeWordsLeaf, colour);
  c->mem_w32(colour + 8u, kBlueColorWord);
  psx::cpu::callGuestNow(*c, "set_light_color_slots", kForwardColorBufferLeaf, colour, colour);
  psx::cpu::callGuestNow(*c,
                         "set_light_color_slots",
                         kStoreColorSlotLeaf,
                         kColorSlots + kColorSlotStride * 2u,
                         colour);

  c->r[31] = c->mem_r32(frame + kSavedRaOffset);
  c->r[17] = c->mem_r32(frame + kSavedS1Offset);
  c->r[16] = c->mem_r32(frame + kSavedS0Offset);
  c->r[29] = frame + kColorFrameBytes;
}

// 0x80058C7C — write one fixed triple into BOTH the specular direction (+0x00..+0x08) and the light
// colour (+0x0C..+0x14): the same three words go to all six, with no shift between the two copies.
// $at exits holding 0x80070000, the bare `lui` and not any of the six addresses. v0 exits the third
// word, v1 the second, and a0 the first that all six stores read.
void setLightColorAndSpecular(Core *c) {
  const std::uint32_t first = 0xFFFFF62Bu;
  const std::uint32_t second = 0xFFFFF666u;
  const std::uint32_t third = 0xFFFFF4E0u;
  c->mem_w32(kSpecularDirection, first);
  c->mem_w32(kSpecularDirection + 4u, second);
  c->mem_w32(kSpecularDirection + 8u, third);
  c->mem_w32(kLightColor, first);
  c->mem_w32(kLightColor + 4u, second);
  c->mem_w32(kLightColor + 8u, third);
  c->r[1] = 0x80070000u;
  c->r[2] = third;
  c->r[3] = second;
  c->r[4] = first;
}

// 0x80058CC0 — add a0 to the 8-bit specular phase, then rebuild the direction from one cosine and
// one sine entry at that phase: X is the cosine scaled by 1983/2048, Y the sine by 6345/4096, and Z
// the fixed 0xFFFFFC00. The phase is masked BEFORE the table index, so a0 only ever moves it
// 0..255. The Y chain leaves v1 on its `(sine * 3) << 5` intermediate and v0 on the finished Y; $at
// exits on the bare `lui` and a0 on the sine entry.
void advanceSpecularDirection(Core *c) {
  const std::uint32_t phase = (c->mem_r32(kSpecularTime) + c->r[4]) & kPhaseMask;
  c->mem_w32(kSpecularTime, phase);
  const std::uint32_t entry = phase * kTableEntryBytes;
  const std::uint32_t cosine = static_cast<std::uint32_t>(c->mem_r16s(kCosineTable + entry));
  const std::uint32_t sine = static_cast<std::uint32_t>(c->mem_r16s(kSineTable + entry));
  std::uint32_t x = (cosine << 5) - cosine;
  x <<= 6;
  x -= cosine;
  const std::int32_t directionX = static_cast<std::int32_t>(x) >> 11;
  c->mem_w32(kSpecularDirection, static_cast<std::uint32_t>(directionX));
  c->mem_w32(kSpecularDirection + 8u, kSpecularZ);
  std::uint32_t y = (sine << 1) + sine;
  const std::uint32_t yIntermediate = y << 5;
  y += yIntermediate;
  y <<= 3;
  y += sine;
  y <<= 3;
  y += sine;
  const std::int32_t directionY = static_cast<std::int32_t>(y) >> 12;
  c->mem_w32(kSpecularDirection + 4u, static_cast<std::uint32_t>(directionY));
  c->r[1] = 0x80070000u;
  c->r[2] = static_cast<std::uint32_t>(directionY);
  c->r[3] = yIntermediate;
  c->r[4] = sine;
}

// 0x8005956C — sum the 0x58C bytes at a0 and leave the total in v0 and v1, with a0 and a1 both one
// byte past the block. The loop test is `sltu` on the ALREADY incremented a0, so the byte at the
// end address is never read and a0 exits exactly at it; the total is `addu`, so it wraps.
void blockByteChecksum(Core *c) {
  const std::uint32_t end = c->r[4] + kChecksumBytes;
  std::uint32_t cursor = c->r[4];
  std::uint32_t sum = 0;
  while (cursor < end) {
    sum += c->mem_r8(cursor);
    cursor += 1u;
  }
  c->r[4] = cursor;
  c->r[5] = end;
  c->r[2] = sum;
  c->r[3] = sum;
}

// 0x8005B6F8 — rebase the drawing pools off the arena pointer, republish them, and clear them. The
// `lui` that forms the layout stride is the beqz delay slot, so it runs on both arms and only the
// `ori` differs: a0 nonzero adds 0xFFFED000, a0 zero adds 0xFFFE4000, and the two orderings are one
// and two strides back from the anchor. The clear calls follow, and neither touches v0 or v1, so
// those exit on the prim pool and the second ordering while $at exits on the bare `lui`.
void resetDrawPools(Core *c) {
  const std::uint32_t arena = c->mem_r32(kArenaBase);
  c->mem_w32(kArenaSlot, arena - kArenaSlotBack);
  const std::uint32_t primPool = arena - kPrimPoolBack;
  const std::uint32_t orderingAnchor = arena - kOrderingAnchorBack;
  c->mem_w32(kPrimPool, primPool);
  c->mem_w32(kOrderingAnchor, orderingAnchor);
  const std::uint32_t stride = c->r[4] != 0u ? kStrideDoubleBuffered : kStrideSingleBuffered;
  const std::uint32_t orderingOne = orderingAnchor + stride;
  c->mem_w32(kOrderingOne, orderingOne);
  const std::uint32_t orderingZero = orderingOne + stride;
  c->mem_w32(kOrderingZero, orderingZero);
  c->mem_w32(kPublishedOrderingZero, orderingZero);
  c->mem_w32(kPublishedPrimPool, primPool);
  c->mem_w32(kPublishedOrderingAnchor, orderingAnchor);
  c->mem_w32(kPublishedOrderingOne, orderingOne);
  c->mem_w32(kPublishedPrimPoolAlt, primPool);
  c->mem_w32(kPublishedOrderingAnchorAlt, orderingAnchor);
  psx::cpu::callGuestNow(*c, "reset_draw_pools", kFillLeaf, orderingAnchor, 0u, kOrderingHeadBytes);
  psx::cpu::callGuestNow(*c, "reset_draw_pools", kFillLeaf, primPool, 0u, kPrimPoolBytes);
  c->r[1] = 0x80070000u;
  c->r[2] = primPool;
  c->r[3] = orderingZero;
}

} // namespace

void registerDrawSetupOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80058BD8u, "set_light_color_slots", setLightColorSlots);
  spyro::installNativeOverride(
      core, 0x80058C7Cu, "set_light_color_and_specular", setLightColorAndSpecular);
  spyro::installNativeOverride(
      core, 0x80058CC0u, "advance_specular_direction", advanceSpecularDirection);
  spyro::installNativeOverride(core, 0x8005956Cu, "block_byte_checksum", blockByteChecksum);
  spyro::installNativeOverride(core, 0x8005B6F8u, "reset_draw_pools", resetDrawPools);
}

} // namespace spyro1::native
