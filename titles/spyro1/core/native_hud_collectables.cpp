#include "native_hud_collectables.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// g_Hud and the five collectible machines packed at its head. The guest reaches every machine's
// sub-state byte and counter through a negative offset from that machine's own state byte, so the
// offsets below are the guest's own field offsets from g_Hud.
constexpr std::uint32_t kHud = 0x80077FA8u;
constexpr std::uint32_t kGemDisplay = kHud + 0x00u;
constexpr std::uint32_t kDragonDisplay = kHud + 0x01u;
constexpr std::uint32_t kLifeDisplay = kHud + 0x02u;
constexpr std::uint32_t kEggDisplay = kHud + 0x03u;
constexpr std::uint32_t kKeyDisplay = kHud + 0x04u;
constexpr std::uint32_t kGemSubState = kHud + 0x05u;
constexpr std::uint32_t kDragonSubState = kHud + 0x06u;
constexpr std::uint32_t kLifeSubState = kHud + 0x07u;
constexpr std::uint32_t kEggSubState = kHud + 0x08u;
constexpr std::uint32_t kKeySubState = kHud + 0x09u;
constexpr std::uint32_t kGemCounter = kHud + 0x0Cu; // g_Hud.m_GemSteadyTicks
constexpr std::uint32_t kDragonCounter = kHud + 0x10u;
constexpr std::uint32_t kLifeCounter = kHud + 0x14u;
constexpr std::uint32_t kEggCounter = kHud + 0x18u;
constexpr std::uint32_t kKeyCounter = kHud + 0x1Cu;
constexpr std::uint32_t kTally = kHud + 0x20u;
constexpr std::uint32_t kLifeIndex = kHud + 0x28u;
constexpr std::uint32_t kEggCount = kHud + 0x2Cu;
constexpr std::uint32_t kKeyIndex = kHud + 0x30u;
constexpr std::uint32_t kKeyFlag = kHud + 0x34u;
constexpr std::uint32_t kLifeOrbCount = kHud + 0x38u;
constexpr std::uint32_t kSparkTimer = kHud + 0x3Cu;
constexpr std::uint32_t kEggPhase = kHud + 0x40u;
// The life rectangles start twelve entries into the sprite array, which is the 0x60 the guest's own
// rectangle base carries: it addresses them off a `lui 0x8008` with a 0x60 offset and a -0x7BF4
// displacement, and 0x80080000 - 0x7BF4 + 0x60 is this address, not the array's own base.
constexpr std::uint32_t kSpriteRect = kHud + 0x4C4u;
// The guest forms the rectangle's source table as the orb count's own address plus 0x388.
constexpr std::uint32_t kSpriteValues = kLifeOrbCount + 0x388u;

constexpr std::uint32_t kDeltaTime = 0x800756CCu;
constexpr std::uint32_t kLevelIndex = 0x80075964u;
constexpr std::uint32_t kLevelGemCount = 0x80077420u;
constexpr std::uint32_t kLevelTarget = 0x80075810u;
constexpr std::uint32_t kTreasureTotal = 0x80075830u;
constexpr std::uint32_t kWaitCounter = 0x8007582Cu;
constexpr std::uint32_t kWaitTimer = 0x800758E8u;
constexpr std::uint32_t kRuntimeEntry = 0x800758E4u;
constexpr std::uint32_t kCosine = 0x8006CC78u;
constexpr std::uint32_t kSine = 0x8006CBF8u;
constexpr std::uint32_t kSparkIds = 0x8006E77Cu;
constexpr std::uint32_t kFadeIds = 0x8006E798u;
constexpr std::uint32_t kSpriteValueBytes = 0x8006E7B4u;
constexpr std::uint32_t kSparkTimerByte = 0x80078192u;
constexpr std::uint32_t kSparkShadeByte = 0x80078191u;
constexpr std::uint32_t kStampCounter = 0x8007829Du;
constexpr std::uint32_t kStampShade = 0x8007829Au;
constexpr std::uint32_t kLifeLevelByte = 0x800783A2u;
constexpr std::uint32_t kRuntimeArgs = 0x800783B4u;
constexpr std::uint32_t kTailCounter = 0x800783FDu;
constexpr std::uint32_t kTailCosine = 0x800783F8u;
constexpr std::uint32_t kTailSine = 0x800783F9u;

// The sub-state every machine parks on before it steps to the next state, the twenty-tick settle
// the gem, dragon, life, egg and key machines count, the -0x28 a counter starts at when its target
// is already met, and the reciprocal the egg phase's modulo nine is computed with.
constexpr std::uint32_t kSubStateDone = 0x0Du;
constexpr std::uint32_t kSettleTicks = 0x14u;
constexpr std::uint32_t kRestartCounter = 0xFFFFFFD8u;
// 0x80054C68 `lh $v1,-0xbc8($at)` on the level-indexed halfword table, the count the gem
// tally has to land on exactly before the display parks on 4.
constexpr std::uint32_t kGemCollectTarget = 0x8006F438u;
constexpr std::uint32_t kModuloNine = 0x38E38E39u;

// The guest helpers the body still reaches. Each is a `jal` operand of the body rather than an
// address it assembles from a lui, so tools/override_constants.py re-derives it from that `jal`
// itself.
constexpr std::uint32_t kPlayId = 0x80054400u;
constexpr std::uint32_t kFadeId = 0x8005445Cu;
constexpr std::uint32_t kSetSubState = 0x800544A8u;
constexpr std::uint32_t kStepCounter = 0x800542E4u;

// The `jal` each nested call stands for (issue 0150): the callee runs with the `$ra` that `jal`
// leaves, and tools/override_call_sites.py re-derives every site and its callee from the
// executable. Several machines share one `jal`: the gem machine's playing and fading states both
// reach 0x80054D1C, the dragon's reach 0x80054E98, the life's 0x8005517C, the egg's fades
// 0x800554A8 and the key's 0x80055704, each through a `j` with the arguments already set.
constexpr std::uint32_t kGemPlayJal = 0x80054D1Cu;
constexpr std::uint32_t kDragonPlayJal = 0x80054E98u;
constexpr std::uint32_t kLifePlayJal = 0x8005517Cu;
constexpr std::uint32_t kKeyPlayJal = 0x80055704u;
constexpr std::uint32_t kEggFadeJal = 0x800554A8u;
constexpr std::uint32_t kGemStepJal = 0x80054BE0u;
constexpr std::uint32_t kGemSetJal = 0x80054BF0u;
constexpr std::uint32_t kLifeStepJal = 0x80055084u;
constexpr std::uint32_t kLifeSetJal = 0x80055098u;
// The two `jalr`s through the level's runtime entry return to their own pc + 8.
constexpr std::uint32_t kEggRuntimeReturn = 0x800553F4u;
constexpr std::uint32_t kKeyRuntimeReturn = 0x80055610u;
// The body lowers $sp by 0x38, which its egg machine's argument block (0x10($sp)) sits inside.
constexpr std::uint32_t kFrameBytes = 0x38u;
constexpr std::uint32_t kRuntimeBlockOffset = 0x10u;

// The guest registers this body carries across its own call sites: $a3, which the delta-time global
// supplies until a callee leaves something else there, and $s0..$s2, which the retail epilogue
// reloads from the stack, so their entry values are what the differential compares.
struct Hud {
  Core *c;
  std::uint32_t a3;
  std::uint32_t s0;
  std::uint32_t s1;
  std::uint32_t s2;
};

// A callee reading $s0..$s2 sees exactly what the retail body left there, so they are installed
// before each call. The callee's own $v0/$v1 come back in c->r[2]/c->r[3], and every path out of a
// call reaches the body's tail, which recomputes both before the epilogue.
void installSavedRegisters(const Hud &hud) {
  hud.c->r[16] = hud.s0;
  hud.c->r[17] = hud.s1;
  hud.c->r[18] = hud.s2;
}

// $a3 is caller-saved and only some call sites set it, so a call that passes it through hands the
// NEXT such call whatever the previous callee left in it, not the delta time it entered with.
void takeLeftoverArgument(Hud &hud) {
  hud.a3 = hud.c->r[7];
}

// g_LevelGemCount[the level now resident], read fresh at each of the guest's own read points.
std::uint32_t levelGemCount(const Hud &hud) {
  return hud.c->mem_r32(kLevelGemCount + 4u * hud.c->mem_r32(kLevelIndex));
}

// The 14-entry id tables the machines step, sign-extended by the guest's `lh`. The sub-state that
// selects the entry is the one BEFORE the step: a machine's playing path stores sub + 1 and looks
// the id up with the sub it already had, so the id and the store disagree by one on every frame.
std::uint32_t sparkId(Core *c, std::uint32_t sub) {
  return static_cast<std::uint32_t>(c->mem_r16s(kSparkIds + sub * 2u));
}

std::uint32_t fadeId(Core *c, std::uint32_t sub) {
  return static_cast<std::uint32_t>(c->mem_r16s(kFadeIds + sub * 2u));
}

// 0x80054988 — the head of the HUD tick: the egg phase steps one modulo nine by the 0x38E38E39
// reciprocal (a SIGNED 32x32 `mult`, so the high word is the signed one and the sign correction
// comes off $a0's own top bit), and the spark timer counts down by the frame delta while the two
// bytes it feeds come off the cosine table. The shade byte is the table value shifted left 16 and
// back right 25, an ARITHMETIC shift, so a value above 0x7FFF wraps into a negative byte rather
// than keeping its top bits.
void stepSparkTimerAndEggPhase(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t phase = c->mem_r32(kEggPhase) + 1u;
  const std::uint32_t timer = (c->mem_r32(kSparkTimer) - c->mem_r32(kDeltaTime)) & 0xFFu;
  c->mem_w32(kSparkTimer, timer);
  const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(phase)) *
                               static_cast<std::int64_t>(kModuloNine);
  c->lo = static_cast<std::uint32_t>(product);
  c->hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
  const std::int32_t high = static_cast<std::int32_t>(c->hi);
  const std::int32_t quotient = (high >> 1) - (static_cast<std::int32_t>(phase) >> 31);
  c->mem_w32(kEggPhase, phase - static_cast<std::uint32_t>(quotient) * 9u);
  const std::uint32_t cosine = c->mem_r16(kCosine + timer * 2u);
  c->mem_w8(kSparkTimerByte, static_cast<std::uint8_t>(timer));
  c->mem_w8(kSparkShadeByte,
            static_cast<std::uint8_t>(static_cast<std::int32_t>(cosine << 16) >> 25));
}

// 0x80054988's gem machine is a five-arm dispatcher at 0x80054A30 reached by FOUR chained
// compares, not a switch:
//
//   0x80054A30  bnez $v1, 0x80054A6C      display 0 arms, anything else leaves
//   0x80054A6C  bne  $v1, $v0(=1) -> 0x80054AF0
//   0x80054AF0  bne  $v1, $v0(=2) -> 0x80054CA4
//   0x80054CA4  bne  $v1, $v0(=3) -> 0x80054D2C
//   0x80054D2C  (falls through)          display 4 and every value above it
//
// so 1 plays, 2 is the twenty-tick settle, 3 is the tally arm and 4 counts the frame delta.
//
// A NOTE ON THE CENSUS THAT SAID THIS BLOCK WAS DEAD, because it was believed once here. The
// branch-target scan (`scratch/branch_scan.py`, 103,936 text words, 12,726 branch and jump words)
// reported 0 references to 0x80054AF0, 0x80054B4C and 0x80054BB0, and the whole settle arm was
// deleted on that evidence. The scan had TWO defects, and both made its answer a uniform zero,
// which is the shape a real absence also has:
//   1. it read the target out of bits 25..0 for EVERY branch, but only `j`/`jal` carry a 26-bit
//      page-relative target; `beq`/`bne`/`slti`/`bgtz` carry a 16-bit SIGNED word offset in their
//      low half, so the scan was effectively counting `j`/`jal` only;
//   2. it looked for REGIMM at opcode 0, where opcode 0 is SPECIAL (`sll`, `jr`, `break`) and
//      REGIMM — `bltz`/`bgez` — is opcode 1, so no `bltz`/`bgez` was in the census at all.
// Fixed, the same scan reports 0x80054AF0 referenced by 1 (0x80054A6C), 0x80054B4C by 2
// (0x80054B18, 0x80054B28) and 0x80054BB0 by 2 (0x80054B80, 0x80054B98), which is the arm
// 0x80054A6C's own `bne`. The scan's answer was checked against the disassembly at a dozen sites
// after the fix and agrees with it every time; see `docs/issues/0147`.
void stepGemDisplay(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t state = c->mem_r8(kGemDisplay);
  if (state == 0u) {
    if (c->mem_r32(kTally) != levelGemCount(hud)) {
      c->mem_w8(kGemDisplay, 1u);
      c->mem_w8(kGemSubState, 0u);
    }
    return;
  }
  if (state == 1u) {
    const std::uint32_t sub = c->mem_r8(kGemSubState);
    if (sub == kSubStateDone) {
      c->mem_w8(kGemDisplay, 2u);
      c->mem_w8(kGemSubState, 0u);
      c->mem_w32(kGemCounter, 0u);
      if (c->mem_r32(kTally) == levelGemCount(hud)) {
        c->mem_w32(kGemCounter, kRestartCounter);
      }
      return;
    }
    // `0x80054A84 bne $a1,$v0,0x80054AD0` carries `addiu $v0,$v1,1` in its delay slot, and a delay
    // slot runs on the taken path too — so the store at 0x80054AD4 is sub+1, not the 0x0D that
    // 0x80054A7C formed. The id lookup that follows indexes the sub this frame read.
    c->mem_w8(kGemSubState, sub + 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(
        *c, "update_hud_collectables", kGemPlayJal, kPlayId, 0u, 5u, sparkId(c, sub), hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 2u) {
    // The settle. Both entry conditions are the guest's own: the tally must have caught up with
    // the level's gem count AND the sub-state must be back at zero, and either one failing sends
    // the machine to the catch-up side rather than out of the switch.
    if (c->mem_r32(kTally) == levelGemCount(hud) && c->mem_r8(kGemSubState) == 0u) {
      const std::uint32_t stepped = c->mem_r32(kGemCounter) + 1u;
      c->mem_w32(kGemCounter, stepped);
      if (stepped != kSettleTicks) {
        return;
      }
      c->mem_w8(kGemDisplay, 3u);
      c->mem_w8(kGemSubState, kSubStateDone);
      return;
    }
    // The catch-up, at 0x80054B4C. It rewinds the sub-state by one of two strides, steps the
    // tally and then RESETS it, which is what makes this the arm that writes kGemCounter to zero
    // on a path the attract route really takes.
    c->mem_w32(kGemCounter, 0u);
    const std::uint32_t sub = c->mem_r8(kGemSubState);
    // `0x80054B7C slti $v0,$v0,3 ; bnez $v0,0x80054BB0` takes the NARROW rewind when the signed
    // difference is BELOW three, so the wide one needs a difference of three or more AND a
    // sub-state whose low five bits are clear (`andi $v0,$v1,0x1f ; bnez` at 0x80054B94): a
    // sub-state of 0x20 satisfies that test. Either failing gives the narrow one.
    if (static_cast<std::int32_t>(levelGemCount(hud) - c->mem_r32(kTally)) >= 3 &&
        (sub & 0x1Fu) == 0u) {
      c->mem_w8(kGemSubState, sub - 0x20u);
    } else {
      c->mem_w8(kGemSubState, sub - 0x10u);
    }
    // `0x80054BD0 ..BD4` leaves $s0 on the tally's address for the rest of the body, and 0x80054BDC
    // zeroes $a3 for the first call only; the second receives whatever the first left there.
    hud.s0 = kTally;
    hud.a3 = 0u;
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(*c,
                               "update_hud_collectables",
                               kGemStepJal,
                               kStepCounter,
                               0u,
                               4u,
                               c->mem_r32(kTally) + 1u,
                               hud.a3);
    takeLeftoverArgument(hud);
    spyro::callGuestJumpedFrom(*c,
                               "update_hud_collectables",
                               kGemSetJal,
                               kSetSubState,
                               0u,
                               4u,
                               c->mem_r8(kGemSubState),
                               hud.a3);
    takeLeftoverArgument(hud);
    // 0x80054BF8. The tally steps by the distance still to cover: a difference of 0xC9 or more
    // adds eight (the `addiu $v0,$a0,8` in the `j 0x80054C4C` delay slot), from 0x15 up adds one,
    // and below that it adds one only while the sub-state is parked on 0xC0 and is left alone
    // otherwise — `0x80054C34 addiu $v0,$zero,0xC0` is the `beqz`'s delay slot and only feeds the
    // compare, so 0xC0 is never stored.
    const std::uint32_t have = c->mem_r32(kTally);
    const std::int32_t difference = static_cast<std::int32_t>(levelGemCount(hud) - have);
    if (difference >= 0xC9) {
      c->mem_w32(kTally, have + 8u);
    } else if (difference >= 0x15 || c->mem_r8(kGemSubState) == 0xC0u) {
      c->mem_w32(kTally, have + 1u);
    }
    // 0x80054C50: the level's own halfword target, and only an exact match with the tally AND a
    // sub-state of zero parks the display on 4.
    const std::uint32_t target =
        static_cast<std::uint32_t>(c->mem_r16s(kGemCollectTarget + 2u * c->mem_r32(kLevelIndex)));
    if (c->mem_r32(kTally) == target && c->mem_r8(kGemSubState) == 0u) {
      c->mem_w8(kGemDisplay, 4u);
    }
    return;
  }
  if (state == 3u) {
    // 0x80054CAC. A tally that is still short sends the machine back to arming; a tally that has
    // caught up with the sub-state at zero parks it on zero, and anything else plays the FADE
    // table's entry for the sub it already holds.
    if (c->mem_r32(kTally) != levelGemCount(hud)) {
      c->mem_w8(kGemDisplay, 1u);
      return;
    }
    const std::uint32_t sub = c->mem_r8(kGemSubState);
    if (sub == 0u) {
      c->mem_w8(kGemDisplay, 0u);
      return;
    }
    // `0x80054CEC bnez $v0,0x80054CFC` carries `addiu $v0,$v0,-1`, so the byte stored back at
    // 0x80054D04 and the index the lookup forms at 0x80054D08 are both sub-1.
    c->mem_w8(kGemSubState, sub - 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(
        *c, "update_hud_collectables", kGemPlayJal, kPlayId, 0u, 5u, fadeId(c, sub - 1u), hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 4u) {
    // 0x80054D2C. `slti $v0,$v0,0xf0` is a SIGNED compare on the summed counter, so a counter that
    // wrapped past 0x7FFFFFFF is negative and does not park the display.
    const std::uint32_t counter = c->mem_r32(kGemCounter) + hud.a3;
    c->mem_w32(kGemCounter, counter);
    if (static_cast<std::int32_t>(counter) >= 0xF0) {
      c->mem_w8(kGemDisplay, 3u);
      c->mem_w8(kGemSubState, kSubStateDone);
    }
    return;
  }
  // 0x80054D2C opens with `bne $v1,$v0(4),0x80054D5C` and no delay-slot work, so every display
  // value above four leaves the machine untouched. A fifth state is not a fifth arm.
}

// 0x80054D5C — the dragon machine, behind a byte counter that steps eight per update and takes its
// shade off the same cosine table. It runs the gem machine's four states without a tally, and
// unlike the gem machine its state 2 settle parks the sub-state on the done value rather than on
// zero.
void stepDragonDisplay(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t stamp = c->mem_r8(kStampCounter) + 8u;
  c->mem_w8(kStampCounter, static_cast<std::uint8_t>(stamp));
  c->mem_w8(kStampShade,
            static_cast<std::uint8_t>(c->mem_r16(kCosine + (stamp & 0xFFu) * 2u) >> 8));
  const std::uint32_t state = c->mem_r8(kDragonDisplay);
  if (state == 1u) {
    const std::uint32_t sub = c->mem_r8(kDragonSubState);
    if (sub == kSubStateDone) {
      c->mem_w8(kDragonDisplay, 2u);
      c->mem_w8(kDragonSubState, 0u);
      c->mem_w32(kDragonCounter, kRestartCounter);
      return;
    }
    c->mem_w8(kDragonSubState, sub + 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(
        *c, "update_hud_collectables", kDragonPlayJal, kPlayId, 5u, 3u, sparkId(c, sub), hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 2u) {
    const std::uint32_t counter = c->mem_r32(kDragonCounter) + 1u;
    c->mem_w32(kDragonCounter, counter);
    if (counter != kSettleTicks) {
      return;
    }
    c->mem_w8(kDragonDisplay, 3u);
    c->mem_w8(kDragonSubState, kSubStateDone);
    return;
  }
  if (state != 3u) {
    return;
  }
  const std::uint32_t sub = c->mem_r8(kDragonSubState);
  if (sub == 0u) {
    c->mem_w8(kDragonDisplay, 0u);
    return;
  }
  c->mem_w8(kDragonSubState, sub - 1u);
  installSavedRegisters(hud);
  spyro::callGuestJumpedFrom(
      *c, "update_hud_collectables", kDragonPlayJal, kPlayId, 5u, 3u, fadeId(c, sub - 1u), hud.a3);
  takeLeftoverArgument(hud);
}

// 0x80054F90 — the life machine's catch-up: the sub-state steps down by 0x10, the guest's step
// helper takes the index plus one and its setter the sub-state, and the index itself advances only
// once the sub-state has reached 0xC0. The counter is cleared on the way in, whatever the path.
void stepLifeIndex(Hud &hud) {
  Core *c = hud.c;
  hud.a3 = 0u;
  c->mem_w32(kLifeCounter, 0u);
  c->mem_w8(kLifeSubState, c->mem_r8(kLifeSubState) - 0x10u);
  installSavedRegisters(hud);
  spyro::callGuestJumpedFrom(*c,
                             "update_hud_collectables",
                             kLifeStepJal,
                             kStepCounter,
                             8u,
                             2u,
                             c->mem_r32(kLifeIndex) + 1u,
                             hud.a3);
  takeLeftoverArgument(hud);
  spyro::callGuestJumpedFrom(*c,
                             "update_hud_collectables",
                             kLifeSetJal,
                             kSetSubState,
                             8u,
                             2u,
                             c->mem_r8(kLifeSubState),
                             hud.a3);
  takeLeftoverArgument(hud);
  if (c->mem_r8(kLifeSubState) == 0xC0u) {
    c->mem_w32(kLifeIndex, c->mem_r32(kLifeIndex) + 1u);
  }
}

// 0x80054EA0 — the life machine. It republishes the spark timer as a HUD byte and then runs the
// same four states as the gem machine, but gated on the life index against the wait counter and the
// orb count against the wait timer rather than on a tally. State 2's orb-count arm is the one that
// skips the counter step entirely, and it does so by latching the orb count to the wait timer.
void stepLifeDisplay(Hud &hud) {
  Core *c = hud.c;
  c->mem_w8(kLifeLevelByte, static_cast<std::uint8_t>(c->mem_r32(kSparkTimer)));
  const std::uint32_t state = c->mem_r8(kLifeDisplay);
  if (state == 0u) {
    if (c->mem_r32(kLifeIndex) != c->mem_r32(kWaitCounter) ||
        c->mem_r32(kLifeOrbCount) != c->mem_r32(kWaitTimer)) {
      c->mem_w8(kLifeDisplay, 1u);
      c->mem_w8(kLifeSubState, 0u);
    }
    return;
  }
  if (state == 1u) {
    const std::uint32_t sub = c->mem_r8(kLifeSubState);
    if (sub == kSubStateDone) {
      c->mem_w8(kLifeDisplay, 2u);
      c->mem_w8(kLifeSubState, 0u);
      c->mem_w32(kLifeCounter, 0u);
      if (c->mem_r32(kLifeIndex) == c->mem_r32(kWaitCounter) &&
          c->mem_r32(kLifeOrbCount) == c->mem_r32(kWaitTimer)) {
        c->mem_w32(kLifeCounter, kRestartCounter);
      }
      return;
    }
    c->mem_w8(kLifeSubState, sub + 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(
        *c, "update_hud_collectables", kLifePlayJal, kPlayId, 8u, 3u, sparkId(c, sub), hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 2u) {
    if (c->mem_r32(kLifeIndex) == c->mem_r32(kWaitCounter) && c->mem_r8(kLifeSubState) == 0u) {
      if (c->mem_r32(kLifeOrbCount) != c->mem_r32(kWaitTimer)) {
        c->mem_w32(kLifeCounter, 0u);
        c->mem_w32(kLifeOrbCount, c->mem_r32(kWaitTimer));
        return;
      }
      const std::uint32_t counter = c->mem_r32(kLifeCounter) + 1u;
      c->mem_w32(kLifeCounter, counter);
      if (counter != kSettleTicks) {
        return;
      }
      c->mem_w8(kLifeDisplay, 3u);
      c->mem_w8(kLifeSubState, kSubStateDone);
      return;
    }
    stepLifeIndex(hud);
    return;
  }
  if (state != 3u) {
    return;
  }
  if (c->mem_r32(kLifeIndex) != c->mem_r32(kWaitCounter) ||
      c->mem_r32(kLifeOrbCount) != c->mem_r32(kWaitTimer)) {
    c->mem_w8(kLifeDisplay, 1u);
    c->mem_w32(kLifeOrbCount, c->mem_r32(kWaitTimer));
    return;
  }
  const std::uint32_t sub = c->mem_r8(kLifeSubState);
  if (sub == 0u) {
    c->mem_w8(kLifeDisplay, 0u);
    return;
  }
  c->mem_w8(kLifeSubState, sub - 1u);
  installSavedRegisters(hud);
  spyro::callGuestJumpedFrom(
      *c, "update_hud_collectables", kLifePlayJal, kPlayId, 8u, 3u, fadeId(c, sub - 1u), hud.a3);
  takeLeftoverArgument(hud);
}

// 0x80055184 — the life machine's sprite rectangles. One 8-byte rectangle per orb, laid out from an
// interleaved byte pair and ONE PAIR OF WORDS THE BODY READS ONCE: `0x8005519C addiu $a3,$v1,0x388`
// forms the word pointer and nothing in the loop advances it — only $a0 (the destination, +8) and
// $a1 (the byte index, +2) move. So every orb takes its x and y from the same two words and only
// its own byte pair differs; indexing the words by the orb number as well would give every orb past
// the first a position retail never computes. The two attribute halfwords are fixed at 8. The loop
// is a do-while with a SIGNED compare against the orb count, so a count too large to represent
// positively ends it on the first pass that overflows the signed compare.
void rebuildLifeOrbRects(Hud &hud) {
  Core *c = hud.c;
  const std::int32_t count = static_cast<std::int32_t>(c->mem_r32(kLifeOrbCount));
  if (count <= 0) {
    return;
  }
  for (std::int32_t index = 0; index < count; ++index) {
    const std::uint32_t step = static_cast<std::uint32_t>(index);
    const std::uint32_t rect = kSpriteRect + step * 8u;
    const std::uint32_t x =
        c->mem_r8(kSpriteValueBytes + step * 2u) + c->mem_r32(kSpriteValues) - 0x1Du;
    const std::uint32_t y =
        c->mem_r8(kSpriteValueBytes + step * 2u + 1u) + c->mem_r32(kSpriteValues + 4u) - 0x1Cu;
    c->mem_w16(rect, static_cast<std::uint16_t>(x));
    c->mem_w16(rect + 2u, static_cast<std::uint16_t>(y));
    c->mem_w16(rect + 4u, 8u);
    c->mem_w16(rect + 6u, 8u);
  }
}

// 0x800553AC — the one call in the body that goes through a pointer the level loaded rather than a
// fixed address. It takes a mode, a tag, a pointer to a three-word block and a zero; the block's
// first word is the count scaled by 324 less 2500, its second is 0x3F0 and its third is 0x1000,
// which is stored in the `jalr`'s delay slot and so is in place BEFORE the callee runs and is never
// written again. The block lives in the body's own frame, so the call runs with $sp lowered (the
// caller has done that for the whole body) and returns to the `jalr`'s pc + 8.
void callRuntimeWithCount(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t block = c->r[29] + kRuntimeBlockOffset;
  c->mem_w32(block, c->mem_r32(kEggCount) * 324u - 2500u);
  c->mem_w32(block + 4u, 0x3F0u);
  c->mem_w32(block + 8u, 0x1000u);
  installSavedRegisters(hud);
  c->r[31] = kEggRuntimeReturn;
  psx::cpu::callGuestNow(
      *c, "update_hud_collectables", c->mem_r32(kRuntimeEntry), 0x10u, 0x4Du, block, 0u);
  takeLeftoverArgument(hud);
}

// 0x8005533C — the egg machine's counting state. With the count behind the level target its whole
// decision is on the counter WORD, compared signed: a counter of exactly one runs the runtime call,
// a counter below nine — every negative one included, `slti $v0,$v0,9` at 0x800553A8 — just steps
// it, and anything else steps the COUNT and clears the counter. With the count already at the level
// target the machine instead counts twenty ticks, and with the key flag set it clears the counter
// and steps it straight to one.
void stepEggCount(Hud &hud) {
  Core *c = hud.c;
  if (c->mem_r32(kEggCount) == c->mem_r32(kLevelTarget)) {
    if (c->mem_r32(kKeyFlag) == 0u) {
      const std::uint32_t counter = c->mem_r32(kEggCounter);
      if (counter != kSettleTicks) {
        c->mem_w32(kEggCounter, counter + 1u);
        return;
      }
      c->mem_w8(kEggDisplay, 3u);
      c->mem_w8(kEggSubState, kSubStateDone);
      c->mem_w32(kEggCounter, counter + 1u);
      return;
    }
    c->mem_w32(kEggCounter, 1u);
    return;
  }
  const std::int32_t counter = static_cast<std::int32_t>(c->mem_r32(kEggCounter));
  if (counter == 1) {
    callRuntimeWithCount(hud);
  } else if (counter < 9) {
    c->mem_w32(kEggCounter, static_cast<std::uint32_t>(counter) + 1u);
    return;
  } else {
    c->mem_w32(kEggCount, c->mem_r32(kEggCount) + 1u);
    c->mem_w32(kEggCounter, 0u);
  }
  c->mem_w32(kEggCounter, c->mem_r32(kEggCounter) + 1u);
}

// 0x80055228 — the egg machine. Its state 2 is the one that calls through the runtime entry the
// level loaded rather than a fixed address, so it is split out above; the rest is the gem machine's
// four states gated on the egg count against the level target and the key flag, and its playing
// paths negate the id they pass, which is the one thing none of the other four machines does.
void stepEggDisplay(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t state = c->mem_r8(kEggDisplay);
  if (state == 0u) {
    if (c->mem_r32(kEggCount) != c->mem_r32(kLevelTarget) || c->mem_r32(kKeyFlag) != 0u) {
      c->mem_w8(kEggDisplay, 1u);
      c->mem_w8(kEggSubState, 0u);
    }
    return;
  }
  if (state == 1u) {
    const std::uint32_t sub = c->mem_r8(kEggSubState);
    if (sub == kSubStateDone) {
      c->mem_w8(kEggDisplay, 2u);
      c->mem_w32(kEggCounter, 0u);
      if (c->mem_r32(kEggCount) == c->mem_r32(kLevelTarget) && c->mem_r32(kKeyFlag) == 0u) {
        c->mem_w32(kEggCounter, kRestartCounter);
      }
      return;
    }
    c->mem_w8(kEggSubState, sub + 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(*c,
                               "update_hud_collectables",
                               kEggFadeJal,
                               kFadeId,
                               0u,
                               0xCu,
                               0u - sparkId(c, sub),
                               hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 2u) {
    stepEggCount(hud);
    return;
  }
  if (state != 3u) {
    return;
  }
  if (c->mem_r32(kEggCount) != c->mem_r32(kLevelTarget) || c->mem_r32(kKeyFlag) != 0u) {
    c->mem_w8(kEggDisplay, 1u);
    return;
  }
  const std::uint32_t sub = c->mem_r8(kEggSubState);
  if (sub == 0u) {
    c->mem_w8(kEggDisplay, 0u);
    return;
  }
  c->mem_w8(kEggSubState, sub - 1u);
  installSavedRegisters(hud);
  spyro::callGuestJumpedFrom(*c,
                             "update_hud_collectables",
                             kEggFadeJal,
                             kFadeId,
                             0u,
                             0xCu,
                             0u - fadeId(c, sub - 1u),
                             hud.a3);
  takeLeftoverArgument(hud);
}

// 0x800555DC — the six runtime calls: one per target, the addresses a SIXTEEN-MEGABYTE step apart
// from 0x00600000 with a fixed 0x8080 added to each, every one taking the same argument table.
// BOTH immediates are `lui`s and both were read an order of magnitude too small: `0x800555E4 lui
// $s0,0x600` is 0x00600000, not 0x00060000, and `0x80055610 lui $v0,0x100` is 0x01000000, not
// 0x00100000 — so the six arguments are 0x00608080 apart, not 0x0008080 apart. The loop counters
// live in $s0..$s2, which the body's epilogue restores, so they are restored here too.
void stepRuntimeTargets(Hud &hud) {
  Core *c = hud.c;
  // `0x800555E4 lui $s0,0x600` is 0x00600000, and `0x80055610 lui $v0,0x100` is 0x01000000 — the
  // step is a `lui` of its own, not a scaled `ori` of a smaller immediate.
  hud.s1 = 0u;
  hud.s2 = 0x8080u;
  hud.s0 = 0x00600000u;
  do {
    // `addiu $s1,$s1,1` is the `jalr`'s delay slot, so the callee already sees the counter stepped.
    hud.s1 += 1u;
    installSavedRegisters(hud);
    c->r[31] = kKeyRuntimeReturn;
    psx::cpu::callGuestNow(*c,
                           "update_hud_collectables",
                           c->mem_r32(kRuntimeEntry),
                           1u,
                           0xCu,
                           kRuntimeArgs,
                           hud.s0 + hud.s2);
    takeLeftoverArgument(hud);
    hud.s0 += 0x01000000u;
  } while (static_cast<std::int32_t>(hud.s1) < 6);
}

// 0x800555AC — the key machine's counting state, in the halves the guest splits it into. A settled
// counter parks the display on 3 with the done sub-state; a counter that has run twelve ticks
// without the index catching up stamps the tail counter with 0x40, latches the total and steps on;
// and a zero counter runs the six runtime calls. Either way the counter is stepped once on the way
// out, so the latched arm lands on one rather than on zero.
void stepKeyCounter(Hud &hud, std::uint32_t total) {
  Core *c = hud.c;
  if (c->mem_r32(kKeyIndex) == total) {
    if (c->mem_r32(kKeyCounter) != kSettleTicks) {
      c->mem_w32(kKeyCounter, c->mem_r32(kKeyCounter) + 1u);
      return;
    }
    c->mem_w8(kKeyDisplay, 3u);
    c->mem_w8(kKeySubState, kSubStateDone);
    c->mem_w32(kKeyCounter, c->mem_r32(kKeyCounter) + 1u);
    return;
  }
  const std::int32_t counter = static_cast<std::int32_t>(c->mem_r32(kKeyCounter));
  if (counter == 0) {
    stepRuntimeTargets(hud);
  } else if (counter < 12) {
    c->mem_w32(kKeyCounter, static_cast<std::uint32_t>(counter) + 1u);
    return;
  } else {
    c->mem_w8(kTailCounter, 0x40u);
    c->mem_w32(kKeyCounter, 0u);
    c->mem_w32(kKeyIndex, total);
  }
  c->mem_w32(kKeyCounter, c->mem_r32(kKeyCounter) + 1u);
}

// 0x800554B0 — the key machine, the last of the five. It latches the treasure total into the key
// index unless that total is 1, and then runs the same four states as the dragon machine with its
// own counter, ids and targets, negating the ids it plays.
void stepKeyDisplay(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t total = c->mem_r32(kTreasureTotal);
  if (total != 1u) {
    c->mem_w32(kKeyIndex, total);
  }
  const std::uint32_t state = c->mem_r8(kKeyDisplay);
  if (state == 0u) {
    if (c->mem_r32(kKeyIndex) != total) {
      c->mem_w8(kKeyDisplay, 1u);
      c->mem_w8(kKeySubState, 0u);
    }
    return;
  }
  if (state == 1u) {
    const std::uint32_t sub = c->mem_r8(kKeySubState);
    if (sub == kSubStateDone) {
      c->mem_w8(kKeyDisplay, 2u);
      c->mem_w32(kKeyCounter, 0u);
      if (c->mem_r32(kKeyIndex) == total) {
        c->mem_w32(kKeyCounter, kRestartCounter);
      }
      return;
    }
    c->mem_w8(kKeySubState, sub + 1u);
    installSavedRegisters(hud);
    spyro::callGuestJumpedFrom(*c,
                               "update_hud_collectables",
                               kKeyPlayJal,
                               kPlayId,
                               0xBu,
                               1u,
                               0u - sparkId(c, sub),
                               hud.a3);
    takeLeftoverArgument(hud);
    return;
  }
  if (state == 2u) {
    stepKeyCounter(hud, total);
    return;
  }
  if (state != 3u) {
    return;
  }
  if (c->mem_r32(kKeyIndex) != total) {
    c->mem_w8(kKeyDisplay, 1u);
    c->mem_w8(kKeySubState, 0u);
    return;
  }
  const std::uint32_t sub = c->mem_r8(kKeySubState);
  if (sub == 0u) {
    c->mem_w8(kKeyDisplay, 0u);
    return;
  }
  c->mem_w8(kKeySubState, sub - 1u);
  installSavedRegisters(hud);
  spyro::callGuestJumpedFrom(*c,
                             "update_hud_collectables",
                             kKeyPlayJal,
                             kPlayId,
                             0xBu,
                             1u,
                             0u - fadeId(c, sub - 1u),
                             hud.a3);
  takeLeftoverArgument(hud);
}

// 0x8005570C — the tail every path in the body reaches, so it alone decides the registers the
// caller sees: v0 is one while the wait timer is under twenty and twenty lower than it otherwise,
// and v1 is the timer word either way. A byte counter steps four and takes a cosine and a sine
// shade off the two tables first, and the counter that guards the timer is capped at 99 on the way
// past.
void advanceFrameStamp(Hud &hud) {
  Core *c = hud.c;
  const std::uint32_t stamp = c->mem_r8(kTailCounter) + 4u;
  c->mem_w8(kTailCounter, static_cast<std::uint8_t>(stamp));
  const std::uint32_t index = (stamp & 0xFFu) * 2u;
  c->mem_w8(kTailCosine, static_cast<std::uint8_t>(c->mem_r16(kCosine + index) >> 7));
  c->mem_w8(kTailSine, static_cast<std::uint8_t>(c->mem_r16(kSine + index) >> 7));
  const std::uint32_t timer = c->mem_r32(kWaitTimer);
  c->r[3] = timer;
  if (static_cast<std::int32_t>(timer) < 0x14) {
    c->r[2] = 1u;
    return;
  }
  const std::uint32_t counter = c->mem_r32(kWaitCounter) + 1u;
  c->mem_w32(kWaitCounter, counter);
  if (static_cast<std::int32_t>(counter) >= 0x64) {
    c->mem_w32(kWaitCounter, 0x63u);
  }
  c->mem_w32(kWaitTimer, timer - 0x14u);
  c->r[2] = timer - 0x14u;
}

// 0x80054988 — the HUD tick: the egg phase and the spark timer, then the gem, dragon, life, egg and
// key machines in the order the body runs them, with the life machine's sprite rectangles between
// the life and egg machines. The saved registers are restored here because the retail epilogue
// restores them from its own frame.
void updateHudCollectables(Core *c) {
  const spyro::PreservedReturnAddress returnAddress(*c);
  const spyro::GuestFrameScope frame(*c, c->r[29] - kFrameBytes);
  const std::uint32_t s0 = c->r[16];
  const std::uint32_t s1 = c->r[17];
  const std::uint32_t s2 = c->r[18];
  Hud hud{c, c->mem_r32(kDeltaTime), s0, s1, s2};
  stepSparkTimerAndEggPhase(hud);
  stepGemDisplay(hud);
  stepDragonDisplay(hud);
  stepLifeDisplay(hud);
  rebuildLifeOrbRects(hud);
  stepEggDisplay(hud);
  stepKeyDisplay(hud);
  advanceFrameStamp(hud);
  c->r[16] = s0;
  c->r[17] = s1;
  c->r[18] = s2;
}

} // namespace

void registerHudCollectableOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80054988u, "update_hud_collectables", updateHudCollectables);
}

} // namespace spyro1::native
