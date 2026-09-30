#include "native_sound_position.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

constexpr std::uint32_t kAudioMonoFlag = 0x80076240u;
constexpr std::int32_t kChannelCeiling = 0x3FFF;

constexpr std::uint32_t kActiveSounds = 0x80075F30u;
constexpr std::uint32_t kStopVoiceWord = 0x8007623Cu;
constexpr std::uint32_t kActiveSoundCount = 24u;
constexpr std::uint32_t kActiveSoundStride = 0x1Cu;
constexpr std::uint32_t kSlotMoby = 0x00u;
constexpr std::uint32_t kSlotSoundId = 0x0Du;
constexpr std::uint32_t kSlotFlags = 0x0Eu;
constexpr std::uint32_t kSlotPitchIncrease = 0x14u;
constexpr std::uint32_t kSlotSoundRef = 0x18u;
constexpr std::uint32_t kStopEverySound = 1u;
constexpr std::uint32_t kStopVoiceSounds = 2u;
constexpr std::uint32_t kVoiceFlag = 0x100u;
constexpr std::uint32_t kClearedSlotFlags = 0x40u;
constexpr std::uint32_t kClearedSoundId = 0xFFu;
constexpr std::uint8_t kStopRequest = 0x7Fu;

constexpr std::uint32_t kSlotGainLeft = 0x04u;
constexpr std::uint32_t kSlotGainRight = 0x06u;
constexpr std::uint32_t kSlotRecordWord = 0x08u;
constexpr std::uint32_t kSlotSearchKey = 0x0Cu;
constexpr std::uint32_t kSlotVolumeLeft = 0x10u;
constexpr std::uint32_t kSlotVolumeRight = 0x12u;
constexpr std::uint32_t kSlotBusyMask = 0xC1u;
constexpr std::uint32_t kSlotKindMask = 0x1Cu;
constexpr std::uint32_t kSlotKindPresets = 0x04u;
constexpr std::uint32_t kSlotKindGated = 0x08u;
constexpr std::uint32_t kSlotKindPresetsAlt = 0x10u;

constexpr std::uint32_t kSoundRecordTable = 0x800761D0u;
constexpr std::uint32_t kSoundStateFlags = 0x800761DCu;
constexpr std::uint32_t kPresetGainLeft = 0x800761E8u;
constexpr std::uint32_t kPresetGainRight = 0x800761EAu;
constexpr std::uint32_t kRecordGain = 0x800761F4u;
constexpr std::uint32_t kGlobalGainScale = 0x8007622Cu;
constexpr std::uint32_t kSlotChangedMask = 0x80076238u;
constexpr std::uint32_t kListenerX = 0x80076DF8u;
constexpr std::uint32_t kListenerZ = 0x80076DFCu;
constexpr std::uint32_t kListenerYaw = 0x80076E20u;

constexpr std::uint32_t kSoundRecordStride = 20u;
constexpr std::uint32_t kRecordWord0 = 0x00u;
constexpr std::uint32_t kRecordWord4 = 0x04u;
constexpr std::uint32_t kRecordLevel = 0x0Au;
constexpr std::uint32_t kRecordRange = 0x0Cu;
constexpr std::uint32_t kRecordStep = 0x0Eu;
constexpr std::uint32_t kRecordKind = 0x10u;

constexpr std::uint32_t kStateUsesRecordGain = 0x02u;
constexpr std::uint32_t kStateUsesPresets = 0x01u;
constexpr std::uint32_t kPresetHalfScaleGain = 0x2000u;
constexpr std::uint32_t kPresetFullScaleGain = 0x3CCCu;
constexpr std::uint32_t kNoFreeSlot = 0xFFFFFFFFu;
constexpr std::uint32_t kNoRecordKind = 0xFFFFFFFFu;
constexpr std::uint32_t kSearchKeySentinel = 0x100u;
constexpr std::uint32_t kSearchKeyProbeOffset =
    0x1C00u; // `sll $v0,$a1,3 ; subu ; sll 2` at 0x80055BA0
constexpr std::uint32_t kProbeBusy = 2u;
constexpr std::uint32_t kProbeMode = 4u;
constexpr std::uint32_t kLimitedProbeMode = 8u;
constexpr std::uint32_t kMobySizeByte = 0x55u;
constexpr std::uint32_t kFallbackSpanLimit = 0x4000u;
constexpr std::uint32_t kFrameSlotMaskWord = 0x10093u;

// The guest callees, each named by the `jal` at the call site the comment gives. A callee is the
// 26-bit field of that instruction rather than a `lui`+immediate pair, and
// tools/override_constants.py re-derives it from the `jal` itself.
constexpr std::uint32_t kProbeActiveSound = 0x80056DC4u; // `jal` at 0x80055AE0
constexpr std::uint32_t kSubtractPosition = 0x8001778Cu; // `jal` at 0x80055B1C
constexpr std::uint32_t kSetVectorLimit = 0x800176C8u;   // `jal` at 0x80055B28
constexpr std::uint32_t kComputeSpan = 0x800171FCu;      // `jal` at 0x80055B34
constexpr std::uint32_t kWrapIntoRange = 0x8006272Cu;    // `jal` at 0x80055D3C
constexpr std::uint32_t kAngleToListener = 0x80016AB4u;  // `jal` at 0x800560C0
constexpr std::uint32_t kAddListenerYaw = 0x80017908u;   // `jal` at 0x800560D8
constexpr std::uint32_t kMarkSlotsChanged = 0x8005C7ACu; // `jal` at 0x8005613C
constexpr std::uint32_t kPositionalStereoVolume = 0x80056C84u;

// `mult` writes 64 bits and the low half is all the guest ever reads back, so a product is
// truncated to 32 bits here exactly where `mflo` truncates it. The shift that follows the two gain
// multiplies is arithmetic, so a negative low half keeps its sign. HI/LO are part of the
// differential's snapshot, so every `mult` the body performs has to HAPPEN, not merely be
// evaluated in C++: a body that computes the same low word leaves a stale HI behind and mismatches
// on every call.
std::uint32_t multiplyLow(Core *c, std::uint32_t left, std::uint32_t right) {
  const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) *
                               static_cast<std::int64_t>(static_cast<std::int32_t>(right));
  c->lo = static_cast<std::uint32_t>(product);
  c->hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
  return c->lo;
}

std::uint32_t lowHalfShift12(Core *c, std::uint32_t value, std::uint32_t factor) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(multiplyLow(c, value, factor)) >> 12);
}

// Every `jal` in this body leaves the guest's $sp at the FRAME pointer, and a callee that reads its
// own frame — 0x80056C84 reads its fifth argument at entry $sp+0x10 — reads it from there. An
// override does not get a frame of its own, so the entry $sp is presented as the frame pointer for
// the length of the call and put back afterwards: the differential compares $sp at the RETURN, and
// both paths return with the caller's own value. Getting this wrong costs a whole offset: a callee
// reading $sp+0x10 then reads 0x90 above the arguments the body wrote, and the divergence shows up
// only in device side effects, never in a register or a RAM byte.
class GuestFrame {
public:
  explicit GuestFrame(Core *c, std::uint32_t frame) : c_(c), saved_(c->r[29]) {
    c_->r[29] = frame;
  }
  ~GuestFrame() {
    c_->r[29] = saved_;
  }
  GuestFrame(const GuestFrame &) = delete;
  GuestFrame &operator=(const GuestFrame &) = delete;

private:
  Core *c_;
  std::uint32_t saved_;
};

// 0x80056C84 (func_80056C84) - stereo volume and pan for one positional sound: attenuates each
// input channel by distance/max distance, pans it by the angle to the sound, clamps both channels
// to [0, 0x3fff] and, with mono audio set, averages the clamped pair. The input SpuVolume is the
// fifth argument, at entry sp+0x10 (0x30($sp) after the body's own -0x20 prologue). Exit state the
// differential checks, which a direct transcription of the source-level description misses:
//   * `move v1, a2` is the delay slot of the max-distance test, so v1 holds the raw angle even on
//     the early return, where v0 keeps whatever the caller left in it.
//   * The panned right channel is stored in the delay slot of its clamp branch, so it is always
//     written, and v1 is reloaded from the output word just before the left clamp: a stereo exit
//     carries the UNCLAMPED left channel, a mono exit the clamped one.
//   * v0 exits as the mono flag (zero) for stereo audio, or as the unsigned average of the
//     clamped pair on the mono path; every clamp path overwrites v0 before that.
// Angles are single bytes and gains single halfwords, so each product fits in 32 bits even though
// the guest multiplies into 64.
void positionalStereoVolume(Core *c) {
  const std::uint32_t volumeOut = c->r[4];
  const std::uint32_t distance = c->r[5];
  const std::uint32_t angleArg = c->r[6];
  const std::uint32_t maxDistance = c->r[7];
  const std::uint32_t volumeIn = c->mem_r32(c->r[29] + 0x10u);

  c->r[3] = angleArg;
  if (maxDistance == 0u) {
    return;
  }

  std::uint32_t mirrorAngle;
  std::uint32_t frontAngle;
  if (((angleArg - 0x40u) & 0xFFu) < 0x80u) {
    mirrorAngle = (0u - angleArg) & 0xFFu;
    frontAngle = angleArg & 0xFFu;
  } else {
    mirrorAngle = (angleArg + 0x80u) & 0xFFu;
    frontAngle = (0x80u - angleArg) & 0xFFu;
  }

  const std::uint32_t volumeScale = (distance << 8) / maxDistance;

  const std::int32_t inputLeft = c->mem_r16s(volumeIn);
  const std::uint32_t leftScaled = volumeScale * static_cast<std::uint32_t>(inputLeft);
  const std::int32_t leftGain =
      static_cast<std::int16_t>(static_cast<std::uint32_t>(inputLeft) - (leftScaled >> 8));
  const std::int32_t leftLevel = (static_cast<std::int32_t>(mirrorAngle) * leftGain) >> 7;
  c->mem_w16(volumeOut, static_cast<std::uint16_t>(leftLevel));

  const std::int32_t inputRight = c->mem_r16s(volumeIn + 2u);
  const std::uint32_t rightScaled = volumeScale * static_cast<std::uint32_t>(inputRight);
  const std::int32_t rightGain =
      static_cast<std::int16_t>(static_cast<std::uint32_t>(inputRight) - (rightScaled >> 8));
  const std::int32_t rightLevel = (static_cast<std::int32_t>(frontAngle) * rightGain) >> 7;
  c->mem_w16(volumeOut + 2u, static_cast<std::uint16_t>(rightLevel));

  const std::int32_t leftBeforeClamp = c->mem_r16s(volumeOut);
  c->r[3] = static_cast<std::uint32_t>(leftBeforeClamp);
  if (leftBeforeClamp < 0) {
    c->mem_w16(volumeOut, 0);
  } else if (leftBeforeClamp > kChannelCeiling) {
    c->mem_w16(volumeOut, static_cast<std::uint16_t>(kChannelCeiling));
  }

  const std::int32_t rightBeforeClamp = c->mem_r16s(volumeOut + 2u);
  if (rightBeforeClamp < 0) {
    c->mem_w16(volumeOut + 2u, 0);
  } else if (rightBeforeClamp > kChannelCeiling) {
    c->mem_w16(volumeOut + 2u, static_cast<std::uint16_t>(kChannelCeiling));
  }

  if (c->mem_r32(kAudioMonoFlag) == 0u) {
    c->r[2] = 0u;
    return;
  }

  const std::int32_t clampedRight = c->mem_r16s(volumeOut + 2u);
  const std::int32_t clampedLeft = c->mem_r16s(volumeOut);
  const std::uint32_t averaged =
      (static_cast<std::uint32_t>(clampedRight) + static_cast<std::uint32_t>(clampedLeft)) >> 1;
  c->mem_w16(volumeOut, static_cast<std::uint16_t>(averaged));
  c->mem_w16(volumeOut + 2u, static_cast<std::uint16_t>(averaged));
  c->r[2] = averaged;
  c->r[3] = static_cast<std::uint32_t>(clampedLeft);
}

// 0x800562A4 (func_800562A4) - stop the active sounds a moby owns: every one of the 24 slots whose
// owner is a0 and which pass the a1 filter (a1 == 1 takes them all, a1 == 2 only those whose flags
// carry 0x100) is marked in a stop mask, told to stop through its sound-reference byte, and cleared
// back to a free slot. Two delay slots carry state a source-level reading misses: the mask OR is
// the delay slot of the reference-pointer test, so a slot is stopped whether or not it had a
// reference to write through, and the final stop-word store is the delay slot of the return, so
// every path leaves v0 as the merged stop mask and v1 as 0x8007623C, the address it was written at.
void stopMobySounds(Core *c) {
  const std::uint32_t moby = c->r[4];
  const std::uint32_t stopType = c->r[5];

  std::uint32_t toStop = 0;
  for (std::uint32_t slot = 0; slot < kActiveSoundCount; ++slot) {
    const std::uint32_t base = kActiveSounds + slot * kActiveSoundStride;
    if (c->mem_r32(base + kSlotMoby) != moby) {
      continue;
    }
    if (stopType != kStopEverySound) {
      if (stopType != kStopVoiceSounds) {
        continue;
      }
      if ((c->mem_r16(base + kSlotFlags) & kVoiceFlag) == 0u) {
        continue;
      }
    }
    toStop |= 1u << slot;

    const std::uint32_t soundRef = c->mem_r32(base + kSlotSoundRef);
    if (soundRef != 0u) {
      c->mem_w8(soundRef, kStopRequest);
    }
    c->mem_w32(base + kSlotSoundRef, 0);
    c->mem_w16(base + kSlotFlags, static_cast<std::uint16_t>(kClearedSlotFlags));
    c->mem_w32(base + kSlotMoby, 0);
    c->mem_w32(base + kSlotPitchIncrease, 0);
    c->mem_w8(base + kSlotSoundId, static_cast<std::uint8_t>(kClearedSoundId));
  }

  const std::uint32_t merged = c->mem_r32(kStopVoiceWord) | toStop;
  c->mem_w32(kStopVoiceWord, merged);
  c->r[2] = merged;
  c->r[3] = kStopVoiceWord;
}

// 0x80055A78 (func_80055A78) — bind one of the 24 active positional sounds to a moby: pick a free
// slot, describe it from the sound record a0 names, derive its stereo gains, and hand them to
// 0x80056C84. a0 is the sound id, a1 the moby, a2 a mode, and a3 a byte the chosen slot index is
// written through — 0x7F when the moby's own size limit rejects the sound instead.
//
// What a source-level reading of the call gets wrong, all of it the differential checks:
//   * The mode dispatch tests mode == 8 BEFORE mode == 4, and `addiu v1, 2` is the DELAY SLOT of
//     the `jal` at 0x80055AE0, so it runs BEFORE 0x80056DC4 does. The `bne`/`beq` that follows
//     therefore compares against the callee's v1, not against 2, wherever 0x80056DC4 writes v1.
//   * The free-slot search at 0x80055B88 also tracks the slot holding the smallest kSlotSearchKey
//     in a1 — and a1 is never read after the loop falls out, so only the FIRST free slot ever
//     reaches the body. The tracking is kept anyway because it still sets v1, which is what the
//     no-free-slot exit returns.
//   * $fp and $s6 are only computed on the limited-probe path, yet they are 0x80056C84's distance
//     and max distance, so every other mode hands that callee the CALLER's own $fp and a3. The
//     frame restores both on the way out; the ARGUMENTS are what has to match.
//   * Only the gated kind (flags & 0x1C == 8) reaches the pan and volume call; the two preset kinds
//     jump straight past it to the shared tail.
//
// v0 is 0 on every path except the limit rejection, which leaves 0x7F. v1 is 2 on the two probe
// rejections, the search key when nothing was free, the moby's size byte on the limit rejection,
// the changed-slot mask when a3 is null, and the sound-record table pointer when a3 is not.
//
// THE FRAME WORD AT +0x1C IS A FLAG WORD READ BY 0x8005C7AC, NOT A MASK OF THIS BODY'S OWN.
// kFrameSlotMaskWord is `lui 1; ori 0x93` — 0x00010093, five digits, and one dropped zero makes it
// 0x1093, whose only bit-0x10000 difference is whether 0x8005CFEC reaches its first 0x8005C588
// call. That call writes the SPU volume block at 0x1F801C00 + 2*($s3|7), so the frame lives inside
// the dead-stack window the differential never compares and the loss shows up ONLY as one missing
// device write: every register, every compared RAM byte and the whole rest of the write log match.
// $s3 is the loop's voice counter *8, a multiple of 8, so both 0x8005C588 calls land on fixed
// addresses 0x1F801C0E and 0x1F801C06 no matter which slot was chosen.
void assignActiveSoundSlot(Core *c) {
  const std::uint32_t soundId = c->r[4];
  const std::uint32_t moby = c->r[5];
  const std::uint32_t mode = c->r[6];
  const std::uint32_t slotOut = c->r[7];
  // The body opens a 0x90-byte frame, so every 0xNN($sp) it names is the FRAME pointer, not the sp
  // the caller left. c->r[29] itself is compared by the differential and must not move.
  const std::uint32_t frame = c->r[29] - 0x90u;
  std::uint32_t v1 = c->r[3];
  std::uint32_t span = c->r[30];
  // $s6, 0x80056C84's max distance, is only computed on the limited-probe path, so every other mode
  // hands that callee the CALLER's own $s6 — which the body's epilogue restores from the stack and
  // which nothing in between writes.
  std::uint32_t spanLimit = c->r[22];

  if (mode == kProbeMode || mode == kLimitedProbeMode) {
    // `addiu $v1, $zero, 2` sits AFTER the `jal` on BOTH mode paths (0x80055AE8 and 0x80055B08),
    // so it is the comparison's operand and never the callee's input: nothing writes $v1 before
    // 0x80056DC4 runs, and the test that follows is against the constant 2 either way.
    const GuestFrame guestFrame(c, frame);
    psx::cpu::callGuestNow(
        *c, "assign_active_sound_slot", kProbeActiveSound, moby, soundId, mode, slotOut);
    v1 = kProbeBusy;
    if (c->r[2] == v1) {
      c->r[2] = 0;
      return;
    }
    if (mode == kLimitedProbeMode) {
      const std::uint32_t scratch = frame + 0x58u;
      psx::cpu::callGuestNow(
          *c, "assign_active_sound_slot", kSubtractPosition, scratch, moby + 0x0Cu, kListenerX);
      psx::cpu::callGuestNow(*c, "assign_active_sound_slot", kSetVectorLimit, scratch, 4u);
      psx::cpu::callGuestNow(*c, "assign_active_sound_slot", kComputeSpan, scratch, 1u);
      // `sll $fp, $v0, 4`: a shift, so it leaves HI/LO alone.
      span = c->r[2] << 4;
      const std::uint32_t size = c->mem_r8(moby + kMobySizeByte);
      spanLimit = size == 0u ? kFallbackSpanLimit : size << 10;
      if (static_cast<std::int32_t>(spanLimit) < static_cast<std::int32_t>(span)) {
        if (slotOut != 0u) {
          c->mem_w8(slotOut, static_cast<std::uint8_t>(kStopRequest));
        }
        c->r[2] = kStopRequest;
        c->r[3] = size;
        return;
      }
    }
  }

  // The search key is $a1, and the ONLY place it survives to the exit is the no-free path: a slot
  // found leaves the key dead, and every other exit overwrites $v1 before returning. It is 0x100
  // before the scan and then the last slot whose key byte compares low, so it is the search key
  // and never a comparison result.
  std::uint32_t searchKey = kSearchKeySentinel;
  std::uint32_t chosen = kNoFreeSlot;
  for (std::uint32_t slot = 0; slot < kActiveSoundCount; ++slot) {
    const std::uint32_t entry = kActiveSounds + slot * kActiveSoundStride;
    if ((c->mem_r16(entry + kSlotFlags) & kSlotBusyMask) == 0u) {
      chosen = slot;
      break;
    }
    // The key byte is compared against the SAME slot 0x100 slots further on, not against a running
    // best: 0x80055BA0 computes `a1*28` with $a1 the constant 0x100, which is 0x1C00 — 256 slot
    // strides — and adds it to the current entry's OWN offset. A body that instead tracks the best
    // key so far agrees only until the first comparison succeeds, and then follows a different
    // byte for the rest of the scan.
    const std::uint32_t here = c->mem_r8(entry + kSlotSearchKey);
    const std::uint32_t other = c->mem_r8(entry + kSearchKeyProbeOffset + kSlotSearchKey);
    if (here < other) {
      searchKey = slot;
    }
  }
  if (chosen == kNoFreeSlot) {
    c->r[2] = 0;
    c->r[3] = searchKey;
    return;
  }

  const std::uint32_t slotBase = kActiveSounds + chosen * kActiveSoundStride;
  const std::uint32_t record = c->mem_r32(kSoundRecordTable) + soundId * kSoundRecordStride;
  c->mem_w16(slotBase + kSlotFlags, static_cast<std::uint16_t>(mode | 1u));
  c->mem_w32(slotBase + kSlotMoby, moby);
  c->mem_w8(slotBase + kSlotSoundId, static_cast<std::uint8_t>(soundId));

  const std::uint32_t state = c->mem_r32(kSoundStateFlags);
  if ((state & kStateUsesRecordGain) != 0u) {
    c->mem_w32(kSoundStateFlags, state & ~kStateUsesRecordGain);
    c->mem_w32(slotBase + kSlotRecordWord, c->mem_r16(kRecordGain));
  } else {
    c->mem_w32(slotBase + kSlotRecordWord, c->mem_r16(record + kRecordLevel));
  }

  c->mem_w32(frame + 0x1Cu, kFrameSlotMaskWord);
  c->mem_w32(frame + 0x18u, 1u << chosen);
  c->mem_w32(frame + 0x34u, c->mem_r32(record + kRecordWord0));
  const std::uint32_t level = c->mem_r16(record + kRecordLevel);
  c->mem_w16(frame + 0x2Cu, static_cast<std::uint16_t>(level));
  const std::uint32_t range = c->mem_r16(record + kRecordRange);
  const std::uint32_t step = c->mem_r16(record + kRecordStep);

  // The record's kind word picks how the level is walked. Kinds 0, 1 and 2 all call 0x8006272C and
  // then share one `div $zero,$v0,$v1 ; mfhi` block, so the term that moves the level is the
  // REMAINDER of the callee's return divided by the record's range, times the record's step — and
  // both the `div` and the `mult` have to be performed, because HI/LO are compared.
  //
  // KIND 0 IS THE ONLY ONE OF THE THREE THAT REWRITES THE LEVEL BEFORE THE WRAP, and it rewrites
  // it by the record's own step, not by an index: 0x80055D28 `lhu $v1,0xe($a0) ; mult $v1,$v0 ;
  // mflo $t0` where `slti $v0,$v1,2` at 0x80055CF8 left 1 in $v0, so the product is the step and
  // `subu $v0,$a2,$t0` biases the level by exactly that. The shared block then divides the callee's
  // return by the record's RANGE halfword at +0x0C — the same one the outer `beqz $a1` tested — and
  // multiplies the REMAINDER by the step, so the level moves by remainder*step.
  std::uint32_t recordKind = kNoRecordKind;
  if (range != 0u) {
    recordKind = c->mem_r32(record + kRecordKind);
    if (recordKind == 0u) {
      const std::uint32_t reduced = level - step;
      c->mem_w16(frame + 0x2Cu, static_cast<std::uint16_t>(reduced));
      psx::cpu::callGuestNow(
          *c, "assign_active_sound_slot", kWrapIntoRange, reduced, range, level, 1u);
    } else if (recordKind <= 2u) {
      psx::cpu::callGuestNow(
          *c, "assign_active_sound_slot", kWrapIntoRange, record, range, level, 1u);
    }
    if (recordKind <= 2u) {
      cpu_div(c, c->r[2], range);
      const std::uint32_t moved = multiplyLow(c, c->hi, step);
      const std::uint32_t current = c->mem_r16(frame + 0x2Cu);
      c->mem_w16(frame + 0x2Cu,
                 static_cast<std::uint16_t>(recordKind == 2u ? current - moved : current + moved));
    }
  }
  // a1/a2/a3 at the tail's 0x8005C7AC call hold the record's range, level and the constant 1 — but
  // only until 0x8006272C overwrites them on its way out, and 0x8005C7AC drives the SPU volumes
  // from them, so after that call they are read back rather than assumed.
  std::uint32_t tailArg1 = range;
  std::uint32_t tailArg2 = level;
  std::uint32_t tailArg3 = 1u;
  if (recordKind <= 2u) {
    tailArg1 = c->r[5];
    tailArg2 = c->r[6];
    tailArg3 = c->r[7];
  }
  c->mem_w32(frame + 0x38u, c->mem_r32(record + kRecordWord4));

  // The kind bits this slot was just stamped with (mode | 1) choose between the two hard-coded gain
  // pairs and the listener's current pair. A kind this body does not name leaves the frame's gain
  // pair holding whatever the previous frame left there, and the tail below reads it back.
  const std::uint32_t slotKind = c->mem_r16(slotBase + kSlotFlags) & kSlotKindMask;
  const bool gated = slotKind == kSlotKindGated;
  const bool presets = slotKind == kSlotKindPresets || slotKind == kSlotKindPresetsAlt;
  if (gated || presets) {
    const std::uint32_t now = c->mem_r32(kSoundStateFlags);
    if ((now & kStateUsesPresets) != 0u) {
      c->mem_w32(kSoundStateFlags, now & ~kStateUsesPresets);
      const auto left = static_cast<std::uint16_t>(c->mem_r16(kPresetGainLeft));
      const auto right = static_cast<std::uint16_t>(c->mem_r16(kPresetGainRight));
      // The gated kind gives the SpuVolume pair the listener's own two gains; a preset kind repeats
      // the left gain as the pair's right half and keeps the listener's right gain in the frame.
      c->mem_w16(slotBase + kSlotVolumeLeft, left);
      c->mem_w16(slotBase + kSlotVolumeRight, gated ? right : left);
      c->mem_w16(frame + 0x20u, left);
      c->mem_w16(frame + 0x22u, right);
      if (presets) {
        c->mem_w16(slotBase + kSlotGainRight, right);
        c->mem_w16(slotBase + kSlotGainLeft, left);
      }
    } else {
      const auto preset =
          static_cast<std::uint16_t>(gated ? kPresetFullScaleGain : kPresetHalfScaleGain);
      c->mem_w16(slotBase + kSlotVolumeLeft, preset);
      c->mem_w16(slotBase + kSlotVolumeRight, preset);
      c->mem_w16(frame + 0x20u, preset);
      c->mem_w16(frame + 0x22u, preset);
      if (presets) {
        c->mem_w16(slotBase + kSlotGainLeft, preset);
        c->mem_w16(slotBase + kSlotGainRight, preset);
      }
    }

    // Only the gated kind reaches the pan and volume call; the two preset kinds jump straight to
    // the tail below. $a2 is zeroed at 0x80056088 and $a3 holds the listener's x word across the
    // two angle calls, so both are reproduced rather than left holding the level and range from
    // above.
    if (gated) {
      // $a2 is zeroed at 0x80056088 and $a3 holds the listener's x word across the two angle calls,
      // so both are reproduced rather than left holding the level and range from above. The second
      // and third calls take the MOBY POINTER's low byte in $a1 and $a2, not the first call's
      // return: $v0 is loaded once at 0x800560A0 and nothing between the two `andi`s writes it.
      const std::uint32_t listenerX = c->mem_r32(kListenerX);
      tailArg2 = 0u;
      tailArg3 = listenerX;
      psx::cpu::callGuestNow(*c,
                             "assign_active_sound_slot",
                             kAngleToListener,
                             listenerX - c->mem_r32(moby + 0x0Cu),
                             c->mem_r32(kListenerZ) - c->mem_r32(moby + 0x10u),
                             0u,
                             listenerX);
      const std::int32_t yaw = static_cast<std::int32_t>(c->mem_r16(kListenerYaw) << 16);
      psx::cpu::callGuestNow(
          *c, "assign_active_sound_slot", kAddListenerYaw, yaw >> 20, moby & 0xFFu, 0u, listenerX);
      // The `jal`'s delay slot writes the slot's SpuVolume pair at 0x10($sp) BEFORE the call, and
      // 0x80056C84 reads it as its fifth argument, so it is written here and not left to the frame.
      c->mem_w32(frame + 0x10u, slotBase + kSlotVolumeLeft);
      const GuestFrame stereoFrame(c, frame);
      psx::cpu::callGuestNow(*c,
                             "assign_active_sound_slot",
                             kPositionalStereoVolume,
                             frame + 0x20u,
                             span,
                             moby & 0xFFu,
                             spanLimit);
      // $a1 is not reloaded after those three calls, so the tail's arguments are whatever they left
      // rather than the range and level assembled above them.
      tailArg1 = c->r[5];
      tailArg2 = c->r[6];
      tailArg3 = c->r[7];
    }
  }

  const std::int32_t leftGain = c->mem_r16s(frame + 0x20u);
  const std::int32_t rightGain = c->mem_r16s(frame + 0x22u);
  const std::int32_t master = static_cast<std::int32_t>(c->mem_r32(kGlobalGainScale));
  const std::uint32_t scaledLeft =
      lowHalfShift12(c, static_cast<std::uint32_t>(leftGain), static_cast<std::uint32_t>(master));
  const std::uint32_t scaledRight =
      lowHalfShift12(c, static_cast<std::uint32_t>(rightGain), static_cast<std::uint32_t>(master));
  c->mem_w16(frame + 0x20u, static_cast<std::uint16_t>(scaledLeft));
  c->mem_w16(frame + 0x22u, static_cast<std::uint16_t>(scaledRight));

  const GuestFrame tailFrame(c, frame);
  psx::cpu::callGuestNow(*c,
                         "assign_active_sound_slot",
                         kMarkSlotsChanged,
                         frame + 0x18u,
                         tailArg1,
                         tailArg2,
                         tailArg3);
  const std::uint32_t changed = c->mem_r32(kSlotChangedMask) | (1u << chosen);
  c->mem_w32(kSlotChangedMask, changed);
  c->r[3] = changed;

  c->mem_w32(slotBase + kSlotSoundRef, slotOut);
  if (slotOut != 0u) {
    c->mem_w8(slotOut, static_cast<std::uint8_t>(chosen));
    c->r[3] = c->mem_r32(kSoundRecordTable);
    if (c->mem_r32(record + kRecordWord4) != 0u) {
      c->mem_w16(slotBase + kSlotFlags,
                 static_cast<std::uint16_t>(c->mem_r16(slotBase + kSlotFlags) | kVoiceFlag));
    }
  }
  c->r[2] = 0;
}

} // namespace

void registerSoundPositionOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x80056C84u, "positional_stereo_volume", positionalStereoVolume);
  spyro::installNativeOverride(core, 0x800562A4u, "stop_moby_sounds", stopMobySounds);
  spyro::installNativeOverride(
      core, 0x80055A78u, "assign_active_sound_slot", assignActiveSoundSlot);
}

} // namespace spyro1::native
