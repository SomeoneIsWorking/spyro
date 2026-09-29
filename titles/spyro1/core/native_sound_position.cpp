#include "native_sound_position.h"

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

} // namespace

void registerSoundPositionOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x80056C84u, "positional_stereo_volume", positionalStereoVolume);
  spyro::installNativeOverride(core, 0x800562A4u, "stop_moby_sounds", stopMobySounds);
}

} // namespace spyro1::native
