#include "native_sound_position.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

constexpr std::uint32_t kAudioMonoFlag = 0x80076240u;
constexpr std::int32_t kChannelCeiling = 0x3FFF;

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

} // namespace

void registerSoundPositionOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x80056C84u, "positional_stereo_volume", positionalStereoVolume);
}

} // namespace spyro1::native
