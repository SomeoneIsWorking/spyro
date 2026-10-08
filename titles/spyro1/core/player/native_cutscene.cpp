#include "native_cutscene.h"

#include "guest_globals.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

using spyro::guest::kCameraRotationX;
using spyro::guest::kCameraRotationY;
using spyro::guest::kCameraRotationZ;

// g_CutsceneLayout and g_LevelMobys are POINTER globals (cutscene.h, moby.c): the layout holds the
// tick being played, the number of mobys it poses, a 24-byte camera key per pair of ticks, and one
// packed-keyframe list per moby; the moby array itself lives in the level's own data.
constexpr std::uint32_t kCutsceneLayout = 0x80075680u;
constexpr std::uint32_t kLayoutCurrentTick = 0x00u;
constexpr std::uint32_t kLayoutMobyCount = 0x0Cu;
constexpr std::uint32_t kLayoutCameraData = 0x10u;
constexpr std::uint32_t kLayoutMobyData = 0x14u;
constexpr std::uint32_t kCameraKeyBytes = 24u;

// g_Camera (camera.h), base 0x80076DD0: three position words, then the three rotation halfwords.
constexpr std::uint32_t kCameraPositionX = 0x80076DF8u;
constexpr std::uint32_t kCameraPositionY = 0x80076DFCu;
constexpr std::uint32_t kCameraPositionZ = 0x80076E00u;

// A layout moby animates g_Models[index + 1] and is written into g_LevelMobys[index].
constexpr std::uint32_t kModelList = 0x8007637Cu;
constexpr std::uint32_t kModelFirstAnimation = 0x38u;
constexpr std::uint32_t kAnimationProgressPerTick = 0x0Cu;
constexpr std::uint32_t kLevelMobys = 0x80075828u;
constexpr std::uint32_t kMobyBytes = 0x58u;
constexpr std::uint32_t kMobyPositionX = 0x0Cu;
constexpr std::uint32_t kMobyPositionY = 0x10u;
constexpr std::uint32_t kMobyPositionZ = 0x14u;
constexpr std::uint32_t kMobyAnimation = 0x3Cu;
constexpr std::uint32_t kMobyNextAnimation = 0x3Du;
constexpr std::uint32_t kMobyFrame = 0x3Eu;
constexpr std::uint32_t kMobyNextFrame = 0x3Fu;
constexpr std::uint32_t kMobyFrameProgress = 0x40u;

constexpr std::uint32_t kProgressPerTick16 = 16u;
constexpr std::uint32_t kProgressPerTick21 = 21u;
constexpr std::uint32_t kProgressPerTick32 = 32u;
constexpr std::uint32_t kFramesPerAnimation = 60u;
constexpr std::uint32_t kKeyBlendUnits = 64u;
// A keyframe word is three 10-bit fields — z in bits 0..9, y in 10..19, x in 20..29 — each scaled
// by 256. Both masks are `lui` results the guest builds once: t7 = lui 0x3FF0 and
// t5 = lui 0xF + ori 0xFC00, which the 12- and 2-bit shifts then bring into place.
constexpr std::uint32_t kKeyXField = 0x3FF00000u;
constexpr std::uint32_t kKeyYField = 0x000FFC00u;
constexpr std::uint32_t kKeyZField = 0x3FFu;

// One truncated divide and its remainder, the way the guest gets them: the quotient comes out of a
// magic multiply and the remainder out of `value - quotient * divisor`, so both are 32-bit modular.
std::uint32_t cutsceneDivmod(std::uint32_t value, std::uint32_t divisor, std::uint32_t *remainder) {
  const std::int32_t quotient =
      static_cast<std::int32_t>(value) / static_cast<std::int32_t>(divisor);
  *remainder = value - (static_cast<std::uint32_t>(quotient) * divisor);
  return static_cast<std::uint32_t>(quotient);
}

// The `addu` of the two low products and the arithmetic `sra 6` after them: each `mult` keeps only
// its low word, so the sum wraps before the shift that divides it by the blend denominator.
std::uint32_t shiftDownSix(std::uint32_t sum) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(sum) >> 6);
}

// ── 0x8002BFE0 — pose one cutscene tick: copy the layout's camera key for the current tick into
//     g_Camera, then for every moby the layout lists, derive its animation state from the model's
//     progress-per-tick and lerp its position between the two packed keyframes it names. The
//     argument is ignored; the tick comes from the layout, and each progress-per-tick selects both
//     the keyframe step and the sub-keyframe blend, 1/8th, 1/6th, 1/4th or 1/2nd of a keyframe.
//     A keyframe word packs x, y and z as three ten-bit fields scaled by 256, and the three camera
//     rotations are 32-bit key words stored as 16 bits, so those three stores truncate while the
//     positions are whole words.
//     Exit registers, which the differential compares: the loop's closing `slt` leaves v0 at 0 and
//     v1 at the last moby it posed, while the `blez` taken on an empty layout leaves v0 at the moby
//     count and v1 at the layout's camera key array.
void applyCutsceneLayoutTick(Core *c) {
  const std::uint32_t layout = c->mem_r32(kCutsceneLayout);
  const std::int32_t tick = static_cast<std::int32_t>(c->mem_r32(layout + kLayoutCurrentTick));
  const std::uint32_t cameraKeys = c->mem_r32(layout + kLayoutCameraData);
  const std::uint32_t cameraKey =
      cameraKeys + (static_cast<std::uint32_t>(tick >> 1) * kCameraKeyBytes);
  c->mem_w32(kCameraPositionX, c->mem_r32(cameraKey));
  c->mem_w32(kCameraPositionY, c->mem_r32(cameraKey + 4u));
  c->mem_w32(kCameraPositionZ, c->mem_r32(cameraKey + 8u));
  c->mem_w16(kCameraRotationX, static_cast<std::uint16_t>(c->mem_r32(cameraKey + 0x0Cu)));
  c->mem_w16(kCameraRotationY, static_cast<std::uint16_t>(c->mem_r32(cameraKey + 0x10u)));
  c->mem_w16(kCameraRotationZ, static_cast<std::uint16_t>(c->mem_r32(cameraKey + 0x14u)));

  const std::int32_t mobyCount = static_cast<std::int32_t>(c->mem_r32(layout + kLayoutMobyCount));
  c->r[2] = static_cast<std::uint32_t>(mobyCount);
  c->r[3] = cameraKeys;
  if (mobyCount <= 0) {
    return;
  }
  const std::uint32_t mobyBase = c->mem_r32(kLevelMobys);
  for (std::int32_t index = 0; index < mobyCount; ++index) {
    const std::uint32_t slot = static_cast<std::uint32_t>(index);
    const std::uint32_t moby = mobyBase + (slot * kMobyBytes);
    c->r[3] = moby;
    const std::uint32_t firstAnimation =
        c->mem_r32(c->mem_r32(kModelList + (slot * 4u)) + kModelFirstAnimation);
    const std::uint32_t progress = c->mem_r8(firstAnimation + kAnimationProgressPerTick);
    std::uint32_t frame = 0;
    std::uint32_t blend = 0;
    std::uint32_t remainder = 0;
    if (progress == kProgressPerTick16) {
      frame = static_cast<std::uint32_t>(tick >> 3);
      blend = (static_cast<std::uint32_t>(tick) & 7u) << 3;
    } else if (progress == kProgressPerTick21) {
      frame = cutsceneDivmod(static_cast<std::uint32_t>(tick), 6u, &remainder);
      blend = remainder * 10u;
    } else if (progress == kProgressPerTick32) {
      frame = static_cast<std::uint32_t>(tick >> 2);
      blend = (static_cast<std::uint32_t>(tick) & 3u) << 4;
    } else {
      frame = static_cast<std::uint32_t>(tick >> 1);
      blend = (static_cast<std::uint32_t>(tick) & 1u) << 5;
    }
    const std::uint32_t animation = cutsceneDivmod(frame, kFramesPerAnimation, &remainder);
    const std::uint32_t nextFrame = frame + 1u;
    const std::uint32_t nextAnimation = cutsceneDivmod(nextFrame, kFramesPerAnimation, &remainder);
    c->mem_w8(moby + kMobyAnimation, static_cast<std::uint8_t>(animation));
    c->mem_w8(moby + kMobyNextAnimation, static_cast<std::uint8_t>(nextAnimation));
    c->mem_w8(moby + kMobyFrame,
              static_cast<std::uint8_t>(frame - (animation * kFramesPerAnimation)));
    c->mem_w8(moby + kMobyNextFrame,
              static_cast<std::uint8_t>(nextFrame - (nextAnimation * kFramesPerAnimation)));
    c->mem_w8(moby + kMobyFrameProgress, static_cast<std::uint8_t>(blend));

    const std::uint32_t keyWords =
        c->mem_r32(layout + kLayoutMobyData + (slot * 4u)) + (frame * 4u);
    const std::uint32_t packed = c->mem_r32(keyWords);
    const std::uint32_t packedNext = c->mem_r32(keyWords + 4u);
    const std::uint32_t x = (packed & kKeyXField) >> 12;
    const std::uint32_t y = (packed & kKeyYField) >> 2;
    const std::uint32_t z = (packed & kKeyZField) << 8;
    const std::uint32_t nextX = (packedNext & kKeyXField) >> 12;
    const std::uint32_t nextY = (packedNext & kKeyYField) >> 2;
    const std::uint32_t nextZ = (packedNext & kKeyZField) << 8;
    const std::uint32_t rest = kKeyBlendUnits - blend;
    c->mem_w32(moby + kMobyPositionX, shiftDownSix((x * rest) + (nextX * blend)));
    c->mem_w32(moby + kMobyPositionY, shiftDownSix((y * rest) + (nextY * blend)));
    c->mem_w32(moby + kMobyPositionZ, shiftDownSix((z * rest) + (nextZ * blend)));
  }
  c->r[2] = 0;
}

} // namespace

void registerCutsceneOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8002BFE0u, "apply_cutscene_layout_tick", applyCutsceneLayoutTick);
}

} // namespace spyro1::native
