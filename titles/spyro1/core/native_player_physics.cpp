#include "native_player_physics.h"

#include "guest_call.h"
#include "guest_globals.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

using spyro::guest::kCosTable;
using spyro::guest::kPlayerControlFlags;
using spyro::guest::kPlayerState;
using spyro::guest::kSinTable;
using spyro::guest::kSpyro;

// g_Spyro's own fields this module reads and writes: the previous position, the physics velocity
// and the two acceleration axes, plus the three 12-bit rotation channels the level entry packs.
// The channels are named in PACKING order, so channel 0 is the one the settle body steps first and
// the one that becomes the first packed byte, which puts the three addresses in descending memory.
constexpr std::uint32_t kPlayerPreviousPosition = kSpyro + 0x8Cu;
constexpr std::uint32_t kPlayerAccelerationX = kSpyro + 0xF4u;
constexpr std::uint32_t kPlayerAccelerationY = kSpyro + 0xF8u;
constexpr std::uint32_t kPlayerVelocity = kSpyro + 0x100u;
constexpr std::uint32_t kRotationChannel0 = kSpyro + 0x124u; // 0x80078B7C
constexpr std::uint32_t kRotationChannel1 = kSpyro + 0x120u; // 0x80078B78
constexpr std::uint32_t kRotationChannel2 = kSpyro + 0x11Cu; // 0x80078B74
constexpr std::uint32_t kRotationChannelSpring0 = kSpyro + 0x258u;
constexpr std::uint32_t kRotationChannelSpring1 = kSpyro + 0x25Cu;
// The three packed rotation bytes, and the matrices the level entry rebuilds from them: one from
// those bytes, one from the head-look bytes that follow them, and their product.
constexpr std::uint32_t kPackedRotationBytes = kSpyro + 0x0Cu;
constexpr std::uint32_t kBodyRotationMatrix = kPackedRotationBytes + 0x28u;
constexpr std::uint32_t kHeadLookRotationBytes = kSpyro + 0x10u;
constexpr std::uint32_t kCombinedRotationMatrix = kPackedRotationBytes + 0x1BCu;

// The four ground probes read a direction pair each, 0x80 bytes apart, from the sin and cos tables
// guest_globals.h names. The loop takes the X word from one table and the Y word from the other
// with a single index, and the tail then indexes the same two tables by the direction the probe
// mask selects.
constexpr std::uint32_t kProbeX = 0x8006CCB8u;
constexpr std::uint32_t kProbeY = 0x8006CC38u;
constexpr std::uint32_t kProbeMaskTable = 0x8006C714u;
constexpr std::uint32_t kProbeCount = 4u;
constexpr std::uint32_t kProbeStride = 0x80u;
constexpr std::uint32_t kProbeAngle = 0x20u;
constexpr std::uint32_t kProbeAngleStep = 0x40u;
constexpr std::uint32_t kNearProbeDepth = 0xFFFFFEDCu; // addiu v0, zero, -0x124
constexpr std::uint32_t kFarProbeDepth = 0xFFFFFE5Cu;  // addiu v0, zero, -0x1A4

// What the level entry writes outside g_Spyro: one flag it clears, the pair of object-list heads it
// resets twice, the object count its three passes iterate, the state words it primes, and the word
// immediately after m_State that it arms. The count and the flag are displacements off the `lui
// 0x8007` word the body loads them through, so they are written as that word plus the offset.
constexpr std::uint32_t kLui7Word = 0x80070000u;
constexpr std::uint32_t kLevelEntryFlag = kLui7Word + 0x5944u;   // 0x80075944
constexpr std::uint32_t kObjectListA = kLui7Word + 0x7378u;      // 0x80077378
constexpr std::uint32_t kObjectListB = kLui7Word + 0x76D8u;      // 0x800776D8
constexpr std::uint32_t kLevelObjectCount = kLui7Word + 0x56CCu; // 0x800756CC
constexpr std::uint32_t kPlayerStateSecondary = kPlayerState + 4u;
constexpr std::uint32_t kPlayerStatePrimed = kSpyro + 0x1F0u;
constexpr std::uint32_t kStateAfterLevelEntry = 15u;
constexpr std::uint32_t kStateSecondaryValue = 8u;
constexpr std::uint32_t kVectorFillWords = 6u;

// The guest callees the two bodies below call, each named by the `jal` that reaches it. A callee
// is a code address, so it is the 26-bit field of that instruction rather than a `lui`+immediate
// pair, and an address audit re-derives it from the `jal` itself. The targets are named
// for the role each plays in the body that calls it.
constexpr std::uint32_t kAddVectors = 0x80017758u; // `jal` at 0x8003E99C, 0x8003E9AC and 0x8004A8C8
constexpr std::uint32_t kCopyVector = 0x80017700u; // `jal` at 0x8004A8B8
constexpr std::uint32_t kFillVector = 0x800176C8u; // `jal` at 0x8004A8A8
constexpr std::uint32_t kClearVector = 0x800176F0u;      // `jal` at 0x8004A860
constexpr std::uint32_t kMatrixFromAngles = 0x80016D2Cu; // `jal` at 0x8004A9A8 and 0x8004A9BC
constexpr std::uint32_t kMultiplyMatrices = 0x800623D8u; // `jal` at 0x8004A9CC
constexpr std::uint32_t kSegmentHitsWorld = 0x8004AE38u; // `jal` at 0x8003E9C0
// The return address `jal kSegmentHitsWorld` at 0x8003E9C0 writes, which the segment test spills
// as the caller's $ra.
constexpr std::uint32_t kSegmentCallReturn = 0x8003E9C8u;
constexpr std::uint32_t kResetObjectLists = 0x80053708u;    // `jal` at 0x8004A814
constexpr std::uint32_t kSetPlayerState = 0x8003EA68u;      // `jal` at 0x8004A834
constexpr std::uint32_t kInitLevelObject = 0x80043FE4u;     // `jal` at 0x8004A87C
constexpr std::uint32_t kResetObjectState = 0x8004888Cu;    // `jal` at 0x8004A8E4
constexpr std::uint32_t kResetObjectMotion = 0x8003D194u;   // `jal` at 0x8004A918
constexpr std::uint32_t kInitObjectMotion = 0x800499C0u;    // `jal` at 0x8004A920
constexpr std::uint32_t kInitObjectPath = 0x80049660u;      // `jal` at 0x8004A928
constexpr std::uint32_t kUpdateFlameTailLock = 0x80049F3Cu; // `jal` at 0x8004A930
constexpr std::uint32_t kInitObjectTrailing = 0x80049E8Cu;  // `jal` at 0x8004A938

inline constexpr char kSteerOwner[] = "steer_toward_supported_ground";
inline constexpr char kLevelEntryOwner[] = "initialise_level_state";

// The 12-bit signed wrap the body applies to a negated word: take the low twelve bits, then
// subtract a turn when they are the upper half. `slti` decides on the unsigned value it just
// masked, so the half-turn test is on 0x800, not on the sign.
std::int32_t signedTurn12(std::uint32_t negated) {
  const std::uint32_t low = negated & 0xFFFu;
  return low < 0x800u ? static_cast<std::int32_t>(low) : static_cast<std::int32_t>(low) - 0x1000;
}

// The guest's own `sll 2 ; sra 4`. The shift wraps at 32 bits BEFORE the divide, so for a large
// word this is not `word >> 2`, and the two channels of the body use different shift pairs.
std::int32_t channelQuarter(std::uint32_t word) {
  return static_cast<std::int32_t>(word << 2u) >> 4;
}

// The same quarter reached as `sll 4 ; sra 6`, which wraps at a different magnitude.
std::int32_t springQuarter(std::uint32_t word) {
  return static_cast<std::int32_t>(word << 4u) >> 6;
}

// The `lhu ; sll 16 ; sra 20` and `lh ; sra 4` the body reaches the direction tables through, both
// of which are a sign-extended halfword divided by sixteen with the remainder going down.
std::int32_t tableStep(std::uint32_t raw) {
  return static_cast<std::int32_t>(static_cast<std::int16_t>(raw)) >> 4;
}

// 0x8004AE38's first act is to spill the CALLER's callee-saved registers to a fixed global at
// 0x80077DD8, and it is read back by 58 sites in the collision and render paths, so those registers
// are part of this body's observable state and every segment-test call has to arrive with the
// register file the retail body held at that point. s7, gp and fp are spilled too, but the body
// never writes them, so they already carry the caller's values; `ra` is the return address the
// `jal` itself wrote, which the body has to put back before it returns because its own `jr $ra`
// consumes the caller's copy it saved on the way in.
struct SegmentCallRegisters {
  std::uint32_t s0;
  std::uint32_t s1;
  std::uint32_t s2;
  std::uint32_t s3;
  std::uint32_t s4;
  std::uint32_t s5;
  std::uint32_t s6;
  std::uint32_t ra;
};

void setSegmentCallRegisters(Core &c, const SegmentCallRegisters &registers, std::uint32_t sp) {
  c.r[16] = registers.s0;
  c.r[17] = registers.s1;
  c.r[18] = registers.s2;
  c.r[19] = registers.s3;
  c.r[20] = registers.s4;
  c.r[21] = registers.s5;
  c.r[22] = registers.s6;
  c.r[29] = sp;
  c.r[31] = registers.ra;
}

// 0x8003DA08 — settle Spyro's first two rotation channels. Each channel is stepped a quarter of the
// way from its own spring state toward the negated channel, and the moved spring state is added
// back into the channel, so the pair is an underdamped return to the neutral orientation. The two
// channels are independent copies of one recurrence, and the body's own frame words hold the two
// signed wraps between the two halves.
//
// v0 exits holding the SECOND channel's last divide (its spring state's `sll 4 ; sra 6`, computed
// from the spring word as it was read, before the store) and v1 the second channel word the body
// just wrote.
void settlePairedRotationChannels(Core *c) {
  const std::uint32_t frame = c->r[29] - 0x10u;
  const std::uint32_t channel0 = c->mem_r32(kRotationChannel0);
  const std::uint32_t channel1 = c->mem_r32(kRotationChannel1);
  const std::int32_t rest0 = signedTurn12(0u - channel0);
  const std::int32_t rest1 = signedTurn12(0u - channel1);
  c->mem_w32(frame, static_cast<std::uint32_t>(rest0));
  c->mem_w32(frame + 4u, static_cast<std::uint32_t>(rest1));

  const std::uint32_t spring0 = c->mem_r32(kRotationChannelSpring0);
  const std::int32_t moved0 = static_cast<std::int32_t>(spring0) +
                              channelQuarter(static_cast<std::uint32_t>(rest0)) -
                              springQuarter(spring0);
  c->mem_w32(kRotationChannelSpring0, static_cast<std::uint32_t>(moved0));
  c->mem_w32(kRotationChannel0,
             static_cast<std::uint32_t>(static_cast<std::int32_t>(channel0) + (moved0 >> 2)));

  const std::uint32_t spring1 = c->mem_r32(kRotationChannelSpring1);
  const std::int32_t moved1 = static_cast<std::int32_t>(spring1) +
                              channelQuarter(static_cast<std::uint32_t>(rest1)) -
                              springQuarter(spring1);
  c->mem_w32(kRotationChannelSpring1, static_cast<std::uint32_t>(moved1));
  const std::int32_t channel1Out = static_cast<std::int32_t>(channel1) + (moved1 >> 2);
  c->mem_w32(kRotationChannel1, static_cast<std::uint32_t>(channel1Out));

  c->r[2] = static_cast<std::uint32_t>(springQuarter(spring1));
  c->r[3] = static_cast<std::uint32_t>(channel1Out);
}

// 0x8003E90C — push Spyro's acceleration toward the ground he is standing on. Four short vertical
// segments, offset to the four diagonals below him, are tested against the world; the four results
// become a mask that indexes a direction table, and a non-negative entry adds that direction to
// m_Physics.m_Acceleration. The two vectors the body tests are built on its own stack frame and the
// two vector adds are guest calls over them, so the frame words are the call's data, not scratch.
//
// The segment test spills the caller's callee-saved registers to a global the collision paths read,
// so each of the four calls also reproduces the register file the retail body held there. v0 exits
// holding the mask times four on the path that finds no direction, or the Y half of the added
// direction on the path that does; v1 holds the last segment test's second return on the first path
// and the new acceleration word on the second.
void steerTowardSupportedGround(Core *c) {
  const std::uint32_t entrySp = c->r[29];
  const SegmentCallRegisters callers{
      c->r[16], c->r[17], c->r[18], c->r[19], c->r[20], c->r[21], c->r[22], c->r[31]};
  const std::uint32_t frame = entrySp - 0x58u;
  const std::uint32_t near = frame + 0x10u;
  const std::uint32_t far = frame + 0x20u;
  std::uint32_t supported = 0u;
  std::uint32_t segmentV1 = 0u;
  for (std::uint32_t probe = 0; probe < kProbeCount; ++probe) {
    const std::uint32_t offset = probe * kProbeStride;
    const std::uint32_t x = static_cast<std::uint32_t>(tableStep(c->mem_r16(kProbeX + offset)));
    const std::uint32_t y = static_cast<std::uint32_t>(tableStep(c->mem_r16(kProbeY + offset)));
    c->mem_w32(near + 0u, x);
    c->mem_w32(near + 4u, y);
    c->mem_w32(near + 8u, kNearProbeDepth);
    c->mem_w32(far + 0u, x);
    c->mem_w32(far + 4u, y);
    c->mem_w32(far + 8u, kFarProbeDepth);
    psx::cpu::callGuestNow(*c, kSteerOwner, kAddVectors, near, near, kSpyro, c->r[7]);
    psx::cpu::callGuestNow(*c, kSteerOwner, kAddVectors, far, far, kSpyro, c->r[7]);
    setSegmentCallRegisters(*c,
                            SegmentCallRegisters{kProbeAngle + probe * kProbeAngleStep,
                                                 probe,
                                                 kSinTable,
                                                 kProbeX + offset,
                                                 far,
                                                 supported,
                                                 kSpyro,
                                                 kSegmentCallReturn},
                            frame);
    psx::cpu::callGuestNow(*c, kSteerOwner, kSegmentHitsWorld, near, far, c->r[6], c->r[7]);
    segmentV1 = c->r[3];
    if (c->r[2] != 0u) {
      supported |= 1u << probe;
    }
  }
  setSegmentCallRegisters(*c, callers, entrySp);

  c->r[2] = supported << 2;
  const std::int32_t direction =
      static_cast<std::int32_t>(c->mem_r32(kProbeMaskTable + supported * 4u));
  if (direction < 0) {
    c->r[3] = segmentV1;
    return;
  }
  const std::uint32_t slot = static_cast<std::uint32_t>(direction) * 2u;
  const std::int32_t pushX = tableStep(c->mem_r16(kCosTable + slot));
  const std::int32_t pushY = tableStep(c->mem_r16(kSinTable + slot));
  const std::int32_t accelX = static_cast<std::int32_t>(c->mem_r32(kPlayerAccelerationX)) + pushX;
  c->mem_w32(kPlayerAccelerationX, static_cast<std::uint32_t>(accelX));
  const std::int32_t accelY = static_cast<std::int32_t>(c->mem_r32(kPlayerAccelerationY)) + pushY;
  c->mem_w32(kPlayerAccelerationY, static_cast<std::uint32_t>(accelY));
  c->r[2] = static_cast<std::uint32_t>(pushY);
  c->r[3] = static_cast<std::uint32_t>(accelY);
}

// 0x8004A7EC — the per-level initialisation. It clears an entry flag, resets the pair of object
// lists, forces Spyro into the post-load state unless he is already there, primes two state words,
// then makes three passes over the level's objects and rebuilds his orientation matrices from the
// three packed rotation bytes. The velocity is cleared, the previous position is resynced to the
// current one, and the position is advanced by the (now zero) velocity, so the player is placed
// rather than moved.
//
// The two `jal` sites around the middle set no argument register at all, so the four argument
// registers are forwarded exactly as the preceding call left them; the object-list reset is
// likewise called with the two heads SWAPPED the second time, which is a fact about the call and
// not a simplification. v0 and v1 exit holding whatever the last list reset returns.
void initialiseLevelState(Core *c) {
  c->mem_w8(kLevelEntryFlag, 0u);
  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kResetObjectLists, kObjectListA, kObjectListB, c->r[6], c->r[7]);

  if (c->mem_r32(kPlayerState) != kStateAfterLevelEntry) {
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kSetPlayerState, kStateAfterLevelEntry, c->r[5], c->r[6], c->r[7]);
  }

  c->mem_w32(kPlayerStateSecondary, kStateSecondaryValue);
  if (static_cast<std::int32_t>(c->mem_r32(kPlayerStatePrimed)) <= 0) {
    c->mem_w32(kPlayerStatePrimed, 1u);
  }

  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kClearVector, kPlayerVelocity, c->r[5], c->r[6], c->r[7]);

  for (std::uint32_t object = 0; object < c->mem_r32(kLevelObjectCount); ++object) {
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kInitLevelObject, object, c->r[5], c->r[6], c->r[7]);
  }

  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kFillVector, kPlayerVelocity, kVectorFillWords, c->r[6], c->r[7]);
  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kCopyVector, kPlayerPreviousPosition, kSpyro, c->r[6], c->r[7]);
  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kAddVectors, kSpyro, kSpyro, kPlayerVelocity, c->r[7]);

  for (std::uint32_t object = 0; object < c->mem_r32(kLevelObjectCount); ++object) {
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kResetObjectState, c->r[4], c->r[5], c->r[6], c->r[7]);
  }

  for (std::uint32_t object = 0; object < c->mem_r32(kLevelObjectCount); ++object) {
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kResetObjectMotion, c->r[4], c->r[5], c->r[6], c->r[7]);
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kInitObjectMotion, c->r[4], c->r[5], c->r[6], c->r[7]);
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kInitObjectPath, c->r[4], c->r[5], c->r[6], c->r[7]);
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kUpdateFlameTailLock, c->r[4], c->r[5], c->r[6], c->r[7]);
    psx::cpu::callGuestNow(
        *c, kLevelEntryOwner, kInitObjectTrailing, c->r[4], c->r[5], c->r[6], c->r[7]);
  }

  c->mem_w8(
      kPackedRotationBytes + 0u,
      static_cast<std::uint8_t>(static_cast<std::int32_t>(c->mem_r32(kRotationChannel0)) >> 4));
  c->mem_w8(
      kPackedRotationBytes + 1u,
      static_cast<std::uint8_t>(static_cast<std::int32_t>(c->mem_r32(kRotationChannel1)) >> 4));
  c->mem_w8(
      kPackedRotationBytes + 2u,
      static_cast<std::uint8_t>(static_cast<std::int32_t>(c->mem_r32(kRotationChannel2)) >> 4));

  psx::cpu::callGuestNow(*c,
                         kLevelEntryOwner,
                         kMatrixFromAngles,
                         kPackedRotationBytes,
                         kBodyRotationMatrix,
                         0u,
                         c->r[7]);
  psx::cpu::callGuestNow(*c,
                         kLevelEntryOwner,
                         kMatrixFromAngles,
                         kHeadLookRotationBytes,
                         kCombinedRotationMatrix,
                         0u,
                         c->r[7]);
  psx::cpu::callGuestNow(*c,
                         kLevelEntryOwner,
                         kMultiplyMatrices,
                         kBodyRotationMatrix,
                         kCombinedRotationMatrix,
                         kCombinedRotationMatrix,
                         c->r[7]);

  c->mem_w32(kPlayerControlFlags, 0u);
  psx::cpu::callGuestNow(
      *c, kLevelEntryOwner, kResetObjectLists, kObjectListB, kObjectListA, c->r[6], c->r[7]);
}

} // namespace

void registerPlayerPhysicsOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x8003DA08u, "settle_paired_rotation_channels", settlePairedRotationChannels);
  spyro::installNativeOverride(
      core, 0x8003E90Cu, "steer_toward_supported_ground", steerTowardSupportedGround);
  spyro::installNativeOverride(core, 0x8004A7ECu, "initialise_level_state", initialiseLevelState);
}

} // namespace spyro1::native
