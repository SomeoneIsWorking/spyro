#include "native_effect_state.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// Every guest call these two bodies make is spelled at its own call site and passes $a0..$a3
// exactly as the guest left them, so a call that sets no argument of its own forwards the register
// file rather than a recomputed value. Each names its own callee and its own registered override as
// the owner, because the owner is the native caller in a dispatch failure report.

// g_Spyro (spyro.h) is 0x80078A58, but both bodies form their g_Spyro addresses from the rotation
// matrix at +0x34 (`lui $s0,0x8008; addiu $s0,$s0,-0x7574`), so each field is spelled as the offset
// from that one formed address — the displacement the guest itself uses.
constexpr std::uint32_t kSpyroRotation = 0x80078A8Cu;                // +0x34, m_RotationMatrix
constexpr std::uint32_t kSpyroPosition = kSpyroRotation - 0x34u;     // +0x00, m_Position
constexpr std::uint32_t kSpyroPositionZ = kSpyroRotation - 0x2Cu;    // +0x08
constexpr std::uint32_t kSpyroSortingDepth = kSpyroRotation - 0x0Du; // +0x27, m_sortingDepth
constexpr std::uint32_t kSpyroSurfaceBelow = kSpyroRotation + 0x6Cu; // +0xA0, m_surfaceBelowSpyro
constexpr std::uint32_t kSurfaceProximityState =
    kSpyroRotation + 0x13Cu;                                    // +0x170, m_SurfaceProximityState
constexpr std::uint32_t kSpyroFlamePhase = 0x80078AB8u;         // +0x60, unk_0x60
constexpr std::uint32_t kSpyroHeadAnimationSpeed = 0x80078ABCu; // +0x64
constexpr std::uint32_t kSpyroNextHeadFrame = 0x80078A79u;      // +0x21, m_nextHeadAnimationFrame
// +0x25, m_headFrameProgress: 0x80078A7D. Spelled as the offset from the rotation matrix the body
// forms, so the displacement is the guest's own; `0x80049CB4 sb $v0,-0x7583($at)` with $at =
// 0x80080000 is 0x80078A7D, which is 0x80078A8C - 0x0F and not - 0x0D.
constexpr std::uint32_t kSpyroHeadFrameProgress = kSpyroRotation - 0x0Fu;
constexpr std::uint32_t kSpyroBurstMode = 0x80078BF0u;      // +0x198, unk_0x198
constexpr std::uint32_t kSpyroBurstTicks = 0x80078BF8u;     // +0x1A0, unk_0x1a0
constexpr std::uint32_t kSpyroHeadLookTarget = 0x80078BFCu; // +0x1A4, m_HeadLookTarget
// +0x78, m_State. The blocked-animation test indexes spyro_StateDefaultAnimation with THIS word,
// not with the burst mode at +0x198: `0x800499C4 lw $v0,-0x7530($v0)` with $v0 = 0x80080000 is
// 0x80078AD0, and the mode is only read at 0x800499A20, after the test has already branched.
constexpr std::uint32_t kSpyroState = kSpyroRotation + 0x44u; // +0x78
constexpr std::uint32_t kBurstMatrix = 0x80078C20u;           // +0x1C8, m_headRotationMatrix
constexpr std::uint32_t kBurstVectors = 0x8006E238u;     // D_8006E238, four 12-byte local offsets
constexpr std::uint32_t kShadowFanOffsets = 0x8006E2A8u; // D_8006E2A8, the ring's eight fan offsets
constexpr std::uint32_t kHeightThresholdTable = 0x8006E9A4u; // D_8006E9A4, one word per level
constexpr std::uint32_t kAnimationSlot = 0x80075268u; // D_80075268, the animation-detail slot
constexpr std::uint32_t kBurstFrameBytes = 0x40u;
constexpr std::uint32_t kShadowFrameBytes = 0x30u;
constexpr std::uint32_t kBurstPadBit = 0x20u;
constexpr std::uint32_t kBurstMidTicks = 0x10u;
constexpr std::uint32_t kBurstWindowFirst = 0x0Cu;
constexpr std::uint32_t kBurstWindowCount = 0x11u;
constexpr std::int32_t kBurstRefreshTicks = 0x2C;
constexpr std::int32_t kBurstEndTicks = 0x30;
constexpr std::uint32_t kBurstStride = 12u;
constexpr std::uint32_t kShadowSlots = 8u;
constexpr std::uint32_t kShadowStride = 12u;

// g_SpyroFlame, spelled with the names game/render/spyro_flame_recipe.cpp already gives this
// struct: the eight per-part lengths every arm and restart clears, then the fields the burst
// publishes. Only m_IsFlameActive and m_FairyKissTimer have names in the decompilation.
constexpr std::uint32_t kFlameLengths = 0x800786E8u;   // +0x20
constexpr std::uint32_t kFlameActive = 0x80078760u;    // +0x98, m_IsFlameActive
constexpr std::uint32_t kFlameState = 0x80078761u;     // +0x99
constexpr std::uint32_t kFlameSelect = 0x80078762u;    // +0x9A
constexpr std::uint32_t kFlameVariant = 0x80078763u;   // +0x9B, the rand() bit
constexpr std::uint32_t kFlameSuper = 0x80078764u;     // +0x9C, the recipe's superflame word
constexpr std::uint32_t kFairyKissTimer = 0x80078768u; // +0xA0, m_FairyKissTimer

// D_8007AA10 is the ground-shadow state game/render/field_shadow_recipe.cpp reads; its +0x20 ring
// index is the only address the body forms itself, so the rest are offsets from it.
constexpr std::uint32_t kShadowRingIndex = 0x8007AA30u;
constexpr std::uint32_t kShadowHeights = kShadowRingIndex - 0x20u;   // +0x00, eight signed bytes
constexpr std::uint32_t kShadowSlotFlags = kShadowRingIndex - 0x18u; // +0x08, eight bytes
constexpr std::uint32_t kShadowAnchor = kShadowRingIndex - 0x10u;    // +0x10, the anchor position
constexpr std::uint32_t kShadowReference = kShadowRingIndex - 0x08u; // +0x18, the reference height
constexpr std::uint32_t kShadowBias = kShadowRingIndex - 0x04u;      // +0x1C
constexpr std::uint32_t kShadowSuppress = kShadowRingIndex + 0x04u;  // +0x24, non-zero hides it

constexpr std::uint32_t kShadowBlendLimit = 0x7Fu;
constexpr std::uint32_t kShadowNearBand = 0x80u;
constexpr std::int32_t kShadowDropRange = 0x365;
constexpr std::int32_t kShadowLiftScale = 194;

constexpr std::uint32_t kPadDown = 0x80077378u;   // g_Pad, m_Down
constexpr std::uint32_t kLoadStage = 0x80075864u; // g_LoadStage
constexpr std::uint32_t kEmitterFunction = 0x800758E4u;
constexpr std::uint32_t kLevelIndex = 0x80075964u; // g_LevelIndex
constexpr std::uint32_t kCameraType = 0x80076E28u; // g_Camera + 0x58

constexpr std::uint32_t kAnimationDetails =
    0x8006C4A3u; // spyro_AnimationDetails + 3, the last frame
constexpr std::uint32_t kStateDefaultAnimation = 0x8006C470u;   // spyro_StateDefaultAnimation
constexpr std::uint32_t kFlameBlockedInAnimation = 0x8006C558u; // spyro_FlameBlockedInAnimation

// The two registered entries, named so the guest call sites inside the bodies can be spelled as
// offsets from them. A `jal`/`jalr` returns to its own pc + 8, and callGuestNow returns through the
// $ra its caller left, so a callee that keeps a frame — g_SpawnParticle and func_8004D5EC both
// spill one to D_80077DD8 before reading anything — must see the return address the guest's own
// call site would have installed.
constexpr std::uint32_t kUpdateFlameBurst = 0x800499C0u;
constexpr std::uint32_t kUpdateGroundShadow = 0x80049FACu;
// The `jalr` at 0x80049BE0 returns to +0x228 and the one at 0x80049C58 to +0x2A0.
constexpr std::uint32_t kEmitterReturns[2] = {kUpdateFlameBurst + 0x228u,
                                              kUpdateFlameBurst + 0x2A0u};
// The word g_Camera+0x58 is compared against on the mode-2 arm: `lui $v1,0x8000` is the delay slot
// of the `j` at 0x80049A38, which replaces the mode in $v1 before `ori $v1,$v1,9` at 0x80049DA8.
constexpr std::uint32_t kExpectedCameraType = 0x80000009u;
// The `jal` at 0x8004A118 returns to +0x174.
constexpr std::uint32_t kSurfaceProbeReturn = kUpdateGroundShadow + 0x174u;

// The callees, each named by the `jal` at the call site the comment gives. A callee is the 26-bit
// field of that instruction rather than a `lui`+immediate pair, and the address audit
// re-derives it from the `jal` itself. The first seven are owned natively already; the two that
// are not are reached only through this gate.
constexpr std::uint32_t kVecNull = 0x800176F0u;
constexpr std::uint32_t kFill = 0x80016914u;
constexpr std::uint32_t kRand = 0x8006272Cu;
constexpr std::uint32_t kRotate = 0x80017048u; // VecRotateByMatrix
constexpr std::uint32_t kVecAdd = 0x80017758u;
constexpr std::uint32_t kVecCopy = 0x80017700u;
constexpr std::uint32_t kRotateLast = 0x800170C0u;   // VecRotateByLastMatrix
constexpr std::uint32_t kSurfaceProbe = 0x8004D5ECu; // func_8004D5EC
constexpr std::uint32_t kSmoothHeadLook = 0x80049880u;

// The `jal` each nested call in update_flame_burst stands for (issue 0150): the callee runs with
// the `$ra` that `jal` leaves, and a call-site audit re-derives every site and its
// callee from the executable. Two of the callees are reached from more than one site, one per arm.
constexpr std::uint32_t kVecNullBlockedJal = 0x80049A08u;
constexpr std::uint32_t kVecNullJal = 0x80049DBCu; // the arm, the reset and the mode-2 arm share it
constexpr std::uint32_t kRandArmJal = 0x80049AD0u;
constexpr std::uint32_t kRandRefreshJal = 0x80049CDCu;
constexpr std::uint32_t kFillArmJal = 0x80049B18u;
constexpr std::uint32_t kFillRefreshJal = 0x80049D24u;
constexpr std::uint32_t kFillResetJal = 0x80049D60u;
constexpr std::uint32_t kSmoothHeadLookJal = 0x80049DDCu;

// The two emitter placements are two copies of one body at two addresses.
struct EmitterPlacementSites {
  std::uint32_t rotate;
  std::uint32_t add;
  std::uint32_t rotateLast;
};
constexpr EmitterPlacementSites kEmitterPlacements[2] = {
    {0x80049B88u, 0x80049B98u, 0x80049BA8u},
    {0x80049C00u, 0x80049C10u, 0x80049C20u},
};

constexpr const char *kFlameBurst = "update_flame_burst";
constexpr const char *kGroundShadow = "update_ground_shadow";

// The arm at 0x80049A6C..0x80049B1C: enter the burst, publish the head animation's last frame and
// the flame's rand() bit, then clear the eight per-part length bytes. The mode and kFlameSuper are
// both stored from $s0, which holds 1 here, so both writes are that literal.
void armFlameBurst(Core *c) {
  c->mem_w32(kSpyroBurstMode, 1);
  const std::uint32_t mode = c->mem_r32(kSpyroBurstMode);
  c->mem_w32(kSpyroBurstTicks, 0u - 1u);
  c->mem_w32(kSpyroFlamePhase, 0);
  const std::uint32_t slot = c->mem_r8(kAnimationSlot + mode);
  c->mem_w32(kSpyroHeadAnimationSpeed, c->mem_r8(kAnimationDetails + slot * 4u));
  c->mem_w8(kFlameActive, 1);
  c->mem_w8(kFlameState, 0);
  c->mem_w8(kFlameSelect, 1);
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kRandArmJal, kRand, c->r[4], c->r[5], c->r[6], c->r[7]);
  const std::uint32_t fairyKiss = c->mem_r32(kFairyKissTimer);
  c->mem_w8(kFlameVariant, static_cast<std::uint8_t>(c->r[2] & 1u));
  c->mem_w32(kFlameSuper, fairyKiss != 0u ? 1u : 0u);
  c->r[4] = kFlameLengths;
  c->r[5] = 0;
  c->r[6] = 8;
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kFillArmJal, kFill, c->r[4], c->r[5], c->r[6], c->r[7]);
}

// The restart at 0x80049C94..0x80049D2C, on tick 0x2C: rewind the tick count, publish the head
// animation fields, and re-arm the emitter exactly as the arm does, from the same two calls.
void refreshFlameBurst(Core *c) {
  c->mem_w32(kSpyroBurstTicks, 0u - 1u);
  c->mem_w32(kSpyroFlamePhase, 2);
  c->mem_w8(kSpyroHeadFrameProgress, 4);
  c->mem_w8(kSpyroNextHeadFrame, 0);
  c->mem_w8(kFlameActive, 1);
  c->mem_w8(kFlameState, 0);
  c->mem_w8(kFlameSelect, 1);
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kRandRefreshJal, kRand, c->r[4], c->r[5], c->r[6], c->r[7]);
  const std::uint32_t fairyKiss = c->mem_r32(kFairyKissTimer);
  c->mem_w8(kFlameVariant, static_cast<std::uint8_t>(c->r[2] & 1u));
  c->mem_w32(kFlameSuper, fairyKiss != 0u ? 1u : 0u);
  c->r[4] = kFlameLengths;
  c->r[5] = 0;
  c->r[6] = 8;
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kFillRefreshJal, kFill, c->r[4], c->r[5], c->r[6], c->r[7]);
}

// The reset at 0x80049D34..0x80049DBC, on tick 0x30: leave the burst, clearing the flame-active
// byte in the fill's delay slot and the eight length bytes behind it, and republish the animation
// last frame for the animation slot the level currently names.
void resetFlameBurst(Core *c) {
  c->r[16] = kSpyroBurstTicks;
  c->r[5] = 0;
  c->r[4] = kFlameLengths;
  c->r[6] = 8;
  c->mem_w8(kFlameActive, 0);
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kFillResetJal, kFill, c->r[4], c->r[5], c->r[6], c->r[7]);
  const std::uint32_t slot = c->mem_r8(kAnimationSlot);
  c->mem_w32(kSpyroBurstMode, 0);
  c->mem_w32(kSpyroBurstTicks, 0u - 1u);
  c->mem_w32(kSpyroHeadAnimationSpeed, c->mem_r8(kAnimationDetails + slot * 4u));
  c->r[4] = kSpyroHeadLookTarget;
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kVecNullJal, kVecNull, c->r[4], c->r[5], c->r[6], c->r[7]);
}

// One emitter placement, 0x80049B78..0x80049BE4: rotate a burst offset by Spyro's matrix, add his
// position, and hand the two vectors to g_SpawnParticle. The body runs this twice, and the rounds
// differ in more than the offset they read — the first takes its matrix from `$s1+0x30` while `$s0`
// still holds the burst offsets, the second reloads `$s0` with the matrix itself — so both are
// carried. The flag choosing the particle variant is RE-READ before each call, because the previous
// call may have written it.
void placeFlameBurstEmitters(Core *c, std::uint32_t frame) {
  c->r[16] = kBurstVectors;
  for (std::uint32_t round = 0; round < 2u; ++round) {
    if (round == 1u) {
      c->r[16] = kBurstMatrix;
      c->r[17] = kBurstVectors + 0x0Cu;
    }
    const std::uint32_t offset = kBurstVectors + round * kBurstStride;
    c->r[4] = kBurstMatrix;
    c->r[5] = offset;
    c->r[6] = frame + 0x10u;
    const EmitterPlacementSites &sites = kEmitterPlacements[round];
    spyro::callGuestJumpedFrom(
        *c, kFlameBurst, sites.rotate, kRotate, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = frame + 0x10u;
    c->r[5] = frame + 0x10u;
    c->r[6] = kSpyroPosition;
    spyro::callGuestJumpedFrom(
        *c, kFlameBurst, sites.add, kVecAdd, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = offset + 0x18u;
    c->r[16] = frame + 0x20u;
    c->r[5] = c->r[16];
    spyro::callGuestJumpedFrom(
        *c, kFlameBurst, sites.rotateLast, kRotateLast, c->r[4], c->r[5], c->r[6], c->r[7]);
    // `addiu $a0,$zero,1` sits in the delay slot of the `beqz` (0x80049BC0, 0x80049C38), so $a0 is
    // 1 whichever way the flag branches, and only $a1 follows the flag.
    const std::uint32_t fromFairyKiss = c->mem_r32(kFlameSuper);
    c->r[4] = 1u;
    c->r[5] = fromFairyKiss != 0u ? 1u : 0u;
    c->r[6] = frame + 0x10u;
    c->r[7] = c->r[16];
    c->r[31] = kEmitterReturns[round];
    psx::cpu::callGuestNow(
        *c, kFlameBurst, c->mem_r32(kEmitterFunction), c->r[4], c->r[5], c->r[6], c->r[7]);
  }
}

// The body of the burst, 0x80049B28..0x80049C60: clear the select byte on every fourth tick but the
// mid one, place the emitters for the seventeen ticks from 0x0C while the level is still loading,
// re-arm at 0x2C, and leave the burst at 0x30. The `andi $v0,$v1,3` the body computes is the same
// on every arm, so one test stands for both of its sites.
void runFlameBurst(Core *c, std::uint32_t frame) {
  std::uint32_t ticks = c->mem_r32(kSpyroBurstTicks);
  // 0x80049B34 `bne $v1,$v0,0x80049B54` TAKES the branch when the tick is NOT 0x10, and the
  // clear at 0x80049B40 is on the fall-through, so the byte is cleared at tick 0x10 and at no
  // other tick. The `andi $v0,$v1,3` in the branch's delay slot is computed for the NEXT branch at
  // 0x80049B54 on both paths and never gates this store.
  if (ticks == kBurstMidTicks) {
    c->mem_w8(kFlameSelect, 0);
    ticks = c->mem_r32(kSpyroBurstTicks); // 0x80049B44 re-reads it, so the low bits are fresh
  }
  if ((ticks & 3u) == 0u) {
    const std::uint32_t window = ticks - kBurstWindowFirst;
    const bool loading = static_cast<std::int32_t>(c->mem_r32(kLoadStage)) < 0;
    if (window < kBurstWindowCount && loading) {
      placeFlameBurstEmitters(c, frame);
      return;
    }
  }
  const std::int32_t now = static_cast<std::int32_t>(c->mem_r32(kSpyroBurstTicks));
  if ((c->mem_r32(kPadDown) & kBurstPadBit) != 0u && now >= kBurstRefreshTicks) {
    refreshFlameBurst(c);
    return;
  }
  if (now >= kBurstEndTicks) {
    resetFlameBurst(c);
  }
}

// 0x800499C0 — the timed flame burst: a three-phase mode (idle, bursting, finished) driven by the
// body's own tick counter, armed by a held pad bit while the emitter is idle, restarted on tick
// 0x2C and retired on tick 0x30. A state whose default animation blocks flame never arms it.
//
// The exit state the differential reads:
//   * EVERY path ends at 0x80049DDC in the head-look stepper, so its v0 and v1 are this function's
//     exit registers. The blocked-animation path (0x800499F4) jumps straight there, and is the only
//     one that skips the tick increment at 0x80049DC4.
//   * The body leaves $sp 0x40 lower for the two 0x10-byte temporaries that the calls read and that
//     g_SpawnParticle is handed, so the frame is reproduced and given back at the end. $s0/$s1 are
//     carried the way the body leaves them for that call and restored to the entry state the
//     epilogue's own stack restores would leave.
void updateFlameBurst(Core *c) {
  const std::uint32_t frame = c->r[29] - kBurstFrameBytes;
  const std::uint32_t entryRa = c->r[31];
  const std::uint32_t entryS0 = c->r[16];
  const std::uint32_t entryS1 = c->r[17];
  c->r[29] = frame;
  const std::uint32_t mode = c->mem_r32(kSpyroBurstMode);
  const std::uint32_t defaultAnimation =
      c->mem_r8(kStateDefaultAnimation + c->mem_r32(kSpyroState));
  if (c->mem_r8(kFlameBlockedInAnimation + defaultAnimation) != 0u) {
    c->mem_w32(kSpyroBurstMode, 0);
    c->r[4] = kSpyroHeadLookTarget;
    spyro::callGuestJumpedFrom(
        *c, kFlameBurst, kVecNullBlockedJal, kVecNull, c->r[4], c->r[5], c->r[6], c->r[7]);
  } else {
    c->r[17] = kSpyroBurstMode;
    if (mode == 0u) {
      const std::uint32_t held = c->mem_r32(kPadDown);
      c->r[4] = kFlameSelect;
      // 0x80049A58 `sb $zero, ($a0)` is the DELAY SLOT of 0x80049A54 `beqz $v0, 0x80049DC4`, and a
      // delay slot runs whether the branch is taken or not, so the clear happens on BOTH paths
      // and gating it on the pad would be the error.
      c->mem_w8(kFlameSelect, 0);
      // 0x80049A68 `bnez $v0, 0x80049DC4` leaves the arm when the flame is already active; its
      // delay slot loads $s0 with 1 either way, and only the fall-through stores it to the mode.
      if ((held & kBurstPadBit) != 0u && c->mem_r8(kFlameActive) == 0u) {
        c->r[16] = 1;
        armFlameBurst(c);
        c->r[4] = kSpyroHeadLookTarget;
        spyro::callGuestJumpedFrom(
            *c, kFlameBurst, kVecNullJal, kVecNull, c->r[4], c->r[5], c->r[6], c->r[7]);
      }
    } else if (mode == 1u) {
      runFlameBurst(c, frame);
    } else if (c->mem_r32(kCameraType) != kExpectedCameraType) {
      // The word at g_Camera+0x58 is compared against the constant 0x80000009, not against a value
      // derived from the mode: the `lui $v1,0x8000` in the delay slot of the `j` at 0x80049A38
      // overwrites the mode in $v1 before the `ori` at 0x80049DA8 makes the constant.
      c->r[4] = kSpyroHeadLookTarget;
      spyro::callGuestJumpedFrom(
          *c, kFlameBurst, kVecNullJal, kVecNull, c->r[4], c->r[5], c->r[6], c->r[7]);
    }
    c->r[3] = kSpyroBurstTicks;
    c->r[2] = c->mem_r32(kSpyroBurstTicks) + 1u;
    c->mem_w32(kSpyroBurstTicks, c->r[2]);
  }
  spyro::callGuestJumpedFrom(
      *c, kFlameBurst, kSmoothHeadLookJal, kSmoothHeadLook, c->r[4], c->r[5], c->r[6], c->r[7]);
  c->r[16] = entryS0;
  c->r[17] = entryS1;
  c->r[31] = entryRa;
  c->r[29] = frame + kBurstFrameBytes;
}

// 0x80049FAC — maintain Spyro's ground shadow: publish the anchor through his own rotation matrix,
// probe the surface under this ring slot, and record the signed lift and the flags the shadow
// recipe reads.
//
// The exit state is per path, and the paths differ in which register holds what:
//   * The ring index is advanced in the delay slot of the armed test, so it advances on BOTH paths,
//     and it is the base of the two byte stores at the end — which the body re-reads each time, so
//     it is read again here after the surface probe.
//   * When the surface is within 0x80 of the anchor, v0 exits as the scaled lift and v1 as the lift
//     byte's address; otherwise v0 exits as that address and v1 as the lift array itself.
//   * The entry's a0 is captured into $s2 before the first call clobbers $a0, and the armed test
//     reads that captured value, not the call's leftovers. $s0, $s1 and $s2 are what func_8004D5EC
//     spills before it reads anything, so all three are carried in the register file here.
void updateGroundShadow(Core *c) {
  const std::uint32_t frame = c->r[29] - kShadowFrameBytes;
  const std::uint32_t entryRa = c->r[31];
  const std::uint32_t entryS0 = c->r[16];
  const std::uint32_t entryS1 = c->r[17];
  const std::uint32_t entryS2 = c->r[18];
  const std::uint32_t armed = c->r[4];
  c->r[29] = frame;
  c->r[16] = kSpyroRotation;
  c->r[18] = armed;
  // The ring slot's own offset, dropped straight down before the first rotation.
  c->r[4] = kSpyroRotation;
  c->r[5] = frame + 0x10u;
  c->r[6] = frame + 0x10u;
  c->mem_w32(frame + 0x10u, 0);
  c->mem_w32(frame + 0x14u, 0);
  c->mem_w32(frame + 0x18u, 0u - 0x164u);
  psx::cpu::callGuestNow(*c, kGroundShadow, kRotate, c->r[4], c->r[5], c->r[6], c->r[7]);
  c->r[4] = frame + 0x10u;
  c->r[5] = frame + 0x10u;
  c->r[6] = kSpyroPosition;
  psx::cpu::callGuestNow(*c, kGroundShadow, kVecAdd, c->r[4], c->r[5], c->r[6], c->r[7]);
  c->r[4] = kShadowAnchor;
  c->r[5] = frame + 0x10u;
  psx::cpu::callGuestNow(*c, kGroundShadow, kVecCopy, c->r[4], c->r[5], c->r[6], c->r[7]);
  if (c->mem_r8(kSpyroSortingDepth) < kShadowBlendLimit) {
    c->mem_w8(kSpyroSortingDepth, 5);
  }
  const std::uint32_t reference = c->mem_r32(kSpyroSurfaceBelow);
  c->mem_w32(kShadowReference, reference);
  c->mem_w32(kShadowBias, 3);
  const std::int32_t drop =
      static_cast<std::int32_t>(c->mem_r32(kSpyroPositionZ)) - static_cast<std::int32_t>(reference);
  if (drop >= kShadowDropRange) {
    c->mem_w32(kShadowBias, 5);
  }
  const std::int32_t threshold =
      static_cast<std::int32_t>(c->mem_r32(kHeightThresholdTable + c->mem_r32(kLevelIndex) * 4u));
  const std::uint32_t proximity = c->mem_r32(kSurfaceProximityState);
  const std::uint32_t suppress =
      threshold >= static_cast<std::int32_t>(reference) || proximity != 0u ? 1u : 0u;
  c->mem_w32(kShadowSuppress, suppress);
  c->r[17] = kShadowRingIndex;
  const std::uint32_t slot = (c->mem_r32(kShadowRingIndex) + 1u) & (kShadowSlots - 1u);
  c->mem_w32(kShadowRingIndex, slot);
  if (armed != 0u) {
    c->r[4] = kSpyroRotation;
    c->r[5] = kShadowFanOffsets + slot * kShadowStride;
    c->r[6] = frame + 0x10u;
    psx::cpu::callGuestNow(*c, kGroundShadow, kRotate, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = frame + 0x10u;
    c->r[5] = frame + 0x10u;
    c->r[6] = kSpyroPosition;
    psx::cpu::callGuestNow(*c, kGroundShadow, kVecAdd, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = frame + 0x10u;
    c->r[5] = 0x400u;
    const std::uint32_t anchorZ = c->mem_r32(frame + 0x18u);
    c->mem_w32(frame + 0x18u, anchorZ + 0x200u);
    c->r[31] = kSurfaceProbeReturn;
    psx::cpu::callGuestNow(*c, kGroundShadow, kSurfaceProbe, c->r[4], c->r[5], c->r[6], c->r[7]);
    const std::int32_t surface = static_cast<std::int32_t>(c->r[2]);
    c->mem_w32(frame + 0x18u, anchorZ);
    const std::int32_t lift = static_cast<std::int32_t>(anchorZ) - surface;
    const bool near = (static_cast<std::uint32_t>(lift) + kShadowNearBand) < 0x100u;
    const std::uint32_t store = c->mem_r32(kShadowRingIndex);
    if (near) {
      c->mem_w8(kShadowSlotFlags + store, 0);
      std::int32_t scaled =
          (static_cast<std::int32_t>(c->mem_r32(kShadowReference)) - surface) * kShadowLiftScale;
      if (scaled < 0) {
        scaled += 0x1FF;
      }
      const std::uint32_t value = static_cast<std::uint32_t>(scaled >> 9);
      c->mem_w8(kShadowHeights + store, static_cast<std::uint8_t>(value));
      c->r[2] = value;
      c->r[3] = kShadowHeights + store;
    } else {
      c->mem_w8(kShadowSlotFlags + store, 1);
      c->mem_w8(kShadowHeights + store, 0);
      c->r[2] = kShadowHeights + store;
      c->r[3] = kShadowHeights;
    }
  } else {
    const std::uint32_t store = c->mem_r32(kShadowRingIndex);
    c->mem_w8(kShadowSlotFlags + store, 0);
    c->mem_w8(kShadowHeights + store, 0);
    c->r[2] = kShadowHeights + store;
    c->r[3] = kShadowHeights;
  }
  c->r[16] = entryS0;
  c->r[17] = entryS1;
  c->r[18] = entryS2;
  c->r[31] = entryRa;
  c->r[29] = frame + kShadowFrameBytes;
}

} // namespace

void registerEffectStateOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x800499C0u, "update_flame_burst", updateFlameBurst);
  spyro::installNativeOverride(core, 0x80049FACu, "update_ground_shadow", updateGroundShadow);
}

} // namespace spyro1::native
