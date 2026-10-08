#include "native_spu_common_attr.h"

#include "native_execution.h"

#include <cstddef>
#include <cstdint>

namespace spyro1::native {
namespace {

// The live SPU state every group publishes through. The body re-reads it per group
// (`lui $v1, 0x8007 ; lw $v1, 0x3554($v1)`), so a group writes through the pointer the STATE holds
// when that group runs, never one hoisted to entry.
constexpr std::uint32_t kSpuCommonAttrState = 0x80073554u;

// The two retail switch tables the argument's two preset indices index, one case arm per entry:
// `sll $v0, $v1, 2 ; lui $at, 0x8001 ; addu $at, $at, $v0 ; lw $v0, 0x157c($at)` for the first
// pair and `0x159c` for the second. Both are read at run time and neither is written out here,
// because what they hold is not what the arms' layout implies: the first entry of each names the
// arm that leaves the volume alone and entries 1..7 name the seven presets 0x8000..0xE000.
constexpr std::uint32_t kFirstVolumeTable = 0x8001157Cu;
constexpr std::uint32_t kSecondVolumeTable = 0x8001159Cu;

// `sltiu $v0, $v1, 8` admits eight cases per table, and `slti $v0, $a3, 0x80` / `bgez $a3` clamp a
// preset's level into [0, 0x7f] SIGNED before `andi 0x7fff` and the `or` with the preset.
constexpr std::uint32_t kVolumeCaseCount = 8u;
constexpr std::int32_t kVolumeLevelCeiling = 0x7F;
constexpr std::uint32_t kVolumeLevelMask = 0x7FFFu;

// Every preset arm is `j <shared clamp>` with `ori $a1, $zero, imm` in its delay slot, so the
// opcode names the arm and its second word carries the preset.
constexpr std::uint32_t kJumpInstruction = 0x08000000u;
constexpr std::uint32_t kOpcodeMask = 0xFC000000u;
constexpr std::uint32_t kImmediateMask = 0xFFFFu;

// The state halfword the four flag groups set and clear one bit of, in place.
constexpr std::uint32_t kStateFlags = 0x1AAu;

// The mask bit that enters each volume group and the one that selects its preset table.
constexpr std::uint32_t kFirstVolumeSelect = 0x1u;
constexpr std::uint32_t kFirstVolumePreset = 0x4u;
constexpr std::uint32_t kSecondVolumeSelect = 0x2u;
constexpr std::uint32_t kSecondVolumePreset = 0x8u;

// The argument's two (preset index, level) volume pairs and the state halfword each publishes.
constexpr std::uint32_t kFirstVolumeIndex = 0x08u;
constexpr std::uint32_t kFirstVolumeLevel = 0x04u;
constexpr std::uint32_t kFirstVolumeState = 0x180u;
constexpr std::uint32_t kSecondVolumeIndex = 0x0Au;
constexpr std::uint32_t kSecondVolumeLevel = 0x06u;
constexpr std::uint32_t kSecondVolumeState = 0x182u;

// One halfword copied straight through, named by the state slot it lands in.
struct HalfwordField {
  std::uint32_t argOffset;
  std::uint32_t stateOffset;
  std::uint32_t selectBit;
};

constexpr std::size_t kHalfwordFieldCount = 4u;

constexpr HalfwordField kHalfwordFields[kHalfwordFieldCount] = {
    {0x10u, 0x1B0u, 0x40u},
    {0x12u, 0x1B2u, 0x80u},
    {0x1Cu, 0x1B4u, 0x400u},
    {0x1Eu, 0x1B6u, 0x800u},
};

// One flag bit: the argument's 32-bit word is tested for zero, and that sets or clears one bit of
// the state flag halfword.
struct FlagField {
  std::uint32_t argOffset;
  std::uint32_t selectBit;
  std::uint32_t flagBit;
};

constexpr std::size_t kFlagFieldCount = 4u;

constexpr FlagField kFlagFields[kFlagFieldCount] = {
    {0x14u, 0x100u, 0x0004u},
    {0x18u, 0x200u, 0x0001u},
    {0x20u, 0x1000u, 0x0008u},
    {0x24u, 0x2000u, 0x0002u},
};

// The preset a volume index selects. The retail table names an arm of this body's own switch, so
// the preset is read out of the arm the table names rather than restated here: each preset arm
// is `j <shared clamp>` with `ori $a1, $zero, imm` in its delay slot. Any other arm is the
// `lhu $a2, 4($a0)` / `move $a1, $zero` pair an index outside the table's eight cases falls into,
// whose $a1 is zero — and zero is the $a1 the `or` sees on that path too, so one value names both
// the preset arms and the arm that leaves the volume alone.
std::uint32_t volumePreset(Core *c, std::uint32_t table, std::uint32_t index) {
  const std::uint32_t arm = c->mem_r32(table + index * 4u);
  if ((c->mem_r32(arm) & kOpcodeMask) != kJumpInstruction) {
    return 0u;
  }
  return c->mem_r32(arm + 4u) & kImmediateMask;
}

// One volume pair. The preset path reads the level `lh` (SIGNED) and clamps it to [0, 0x7f]; the
// path the index misses reads it `lhu` (UNSIGNED) and lets `andi 0x7fff` alone cut it, so the two
// disagree for a level of 128 or more and for every negative level. The store is
// `(level & 0x7fff) | preset`, and $v0 carries that stored word out with $v1 the state pointer.
void pushVolume(Core *c,
                std::uint32_t indexField,
                std::uint32_t levelField,
                std::uint32_t stateOffset,
                std::uint32_t table,
                bool usePresetTable) {
  std::uint32_t preset = 0u;
  if (usePresetTable) {
    const std::int32_t index = c->mem_r16s(indexField);
    if (static_cast<std::uint32_t>(index) < kVolumeCaseCount) {
      preset = volumePreset(c, table, static_cast<std::uint32_t>(index));
    }
  }
  std::uint32_t level = 0u;
  if (preset == 0u) {
    level = c->mem_r16(levelField) & kVolumeLevelMask;
  } else {
    const std::int32_t signedLevel = c->mem_r16s(levelField);
    if (signedLevel >= kVolumeLevelCeiling) {
      level = static_cast<std::uint32_t>(kVolumeLevelCeiling);
    } else if (signedLevel < 0) {
      level = 0u;
    } else {
      level = static_cast<std::uint32_t>(signedLevel);
    }
  }
  c->r[2] = level | preset;
  c->r[3] = c->mem_r32(kSpuCommonAttrState);
  c->mem_w16(c->r[3] + stateOffset, static_cast<std::uint16_t>(c->r[2]));
}

// One halfword copied straight through, UNSIGNED, to its state slot. $v0 carries the loaded word
// out with $v1 the state pointer.
void pushStateHalfword(Core *c, std::uint32_t argField, std::uint32_t stateOffset) {
  c->r[3] = c->mem_r32(kSpuCommonAttrState);
  c->r[2] = c->mem_r16(argField);
  c->mem_w16(c->r[3] + stateOffset, static_cast<std::uint16_t>(c->r[2]));
}

// One flag bit, set or cleared in place in the state flag halfword. This group leaves $v0 holding
// the STATE POINTER — the register the halfword is addressed through — and the new flag word in
// $v1, the reverse of every other group.
void pushStateFlag(Core *c, std::uint32_t argField, std::uint32_t flagBit) {
  const std::uint32_t state = c->mem_r32(kSpuCommonAttrState);
  const std::uint32_t flags = c->mem_r16(state + kStateFlags);
  c->r[2] = state;
  if (c->mem_r32(argField) != 0u) {
    c->r[3] = flags | flagBit;
  } else {
    c->r[3] = flags & ~flagBit;
  }
  c->mem_w16(state + kStateFlags, static_cast<std::uint16_t>(c->r[3]));
}

// ── 0x8005CC58 (SpuSetCommonAttr) — publish one SpuCommonAttr into the live SPU state.
//     sp -= 0x10 ; a2 = 0 ; t1 = [a0] ; t2 = (t1 == 0) ; t0 = 0 ; ten field groups ; sp += 0x10
// A mask of zero enters EVERY group: each group is reached by `bnez $t2, <group>` before its own
// bit is ever tested, so zero means "publish the whole attribute", not "publish none". A set mask
// enters a group through its own select bit, and every one of those tests is a branch's DELAY
// SLOT, so a group that is skipped still publishes the bit it tested in $v0 — which is what the
// last skipped group leaves behind on the way out. $v1 is the caller's unless a group runs, and
// the first volume group's skip publishes bit 4 rather than its own select bit 1, because the
// table/direct test between them is the delay slot that ran last.
//
// MEASURED, and the coverage is narrower than the body: artisans-walk samples 19 of 19 calls and
// pause-menu 27 of 27, both matching, and across the 46 calls only TWO masks occur — 0xC0 eighteen
// and twenty-six times and 0xC3 once each. So the two volume groups and the two halfword groups
// (0x1B0, 0x1B2) are exercised with several distinct halfword values, the six remaining groups
// are exercised only in their SKIPPED form, and the mask of zero never occurs at all. Unexercised
// therefore, and following the retail disassembly and its data alone:
//
//   * BOTH VOLUME PRESET TABLES. Bits 4 and 8 are clear in both masks, so neither index is ever
//     dispatched and volumePreset never runs. Its input was READ out of the executable instead of
//     assumed, which mattered: the table at 0x8001157C puts the "leave the volume alone" arm at
//     index 0 and the seven presets 0x8000..0xE000 at indices 1..7, so a table of
//     `0x8000 + index * 0x1000` — the reading the arms' layout alone suggests — is wrong on
//     three of its eight entries, and 0x8001159C is shifted the same way.
//   * BOTH CLAMPS. Both indices are 0 and both levels are 15564, so the `slti`/`bgez` clamp never
//     runs; the level reaches the store `lhu`-read and `andi`-cut, which differs from the clamp
//     for any level of 128 or more and for every negative level.
//   * ALL FOUR FLAG GROUPS. Every argument flag word is zero, and their select bits are clear in
//     both masks, so pushStateFlag never runs and the state flag halfword is never written.
//
// None of that is a substitute for the gate, and none of it is claimed to be: the differential
// covers the mask and the stores the game actually makes, and the rest stands on the disassembly.
void setSpuCommonAttr(Core *c) {
  const std::uint32_t arg = c->r[4];
  const std::uint32_t mask = c->mem_r32(arg);
  const bool whole = mask == 0u;

  if (whole || (mask & kFirstVolumeSelect) != 0u) {
    pushVolume(c,
               arg + kFirstVolumeIndex,
               arg + kFirstVolumeLevel,
               kFirstVolumeState,
               kFirstVolumeTable,
               whole || (mask & kFirstVolumePreset) != 0u);
  } else {
    c->r[2] = mask & kFirstVolumePreset;
  }

  if (whole || (mask & kSecondVolumeSelect) != 0u) {
    pushVolume(c,
               arg + kSecondVolumeIndex,
               arg + kSecondVolumeLevel,
               kSecondVolumeState,
               kSecondVolumeTable,
               whole || (mask & kSecondVolumePreset) != 0u);
  } else {
    c->r[2] = mask & kSecondVolumeSelect;
  }

  for (std::size_t index = 0; index < kHalfwordFieldCount; ++index) {
    const HalfwordField &field = kHalfwordFields[index];
    if (whole || (mask & field.selectBit) != 0u) {
      pushStateHalfword(c, arg + field.argOffset, field.stateOffset);
    } else {
      c->r[2] = mask & field.selectBit;
    }
  }

  for (std::size_t index = 0; index < kFlagFieldCount; ++index) {
    const FlagField &field = kFlagFields[index];
    if (whole || (mask & field.selectBit) != 0u) {
      pushStateFlag(c, arg + field.argOffset, field.flagBit);
    } else {
      c->r[2] = mask & field.selectBit;
    }
  }
}

} // namespace

void registerSpuCommonAttrOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x8005CC58u, "spu_set_common_attr", setSpuCommonAttr);
}

} // namespace spyro1::native