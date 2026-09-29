#include "native_player_animation.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

constexpr std::uint32_t kBodyAnimation = 0x80078A70u;          // g_Spyro.m_bodyAnimation (u8)
constexpr std::uint32_t kNextBodyAnimation = 0x80078A71u;      // g_Spyro.m_nextBodyAnimation (u8)
constexpr std::uint32_t kBodyAnimationFrame = 0x80078A76u;     // g_Spyro.m_bodyAnimationFrame (u8)
constexpr std::uint32_t kNextBodyAnimationFrame = 0x80078A77u; // g_Spyro.m_nextBodyAnimationFrame
constexpr std::uint32_t kBodyFrameProgress = 0x80078A7Cu;      // g_Spyro.m_bodyFrameProgress (u8)
constexpr std::uint32_t kAnimationDetails = 0x8006C4A0u; // spyro_AnimationDetails (4-byte stride)
constexpr std::uint32_t kAnimationEntryBytes = 4u;
constexpr std::uint32_t kSeparateTailAnimation = 0x80078C40u;
constexpr std::uint32_t kFlameableFrames = 0x80078C44u;
constexpr std::uint32_t kFlameBlockedInAnimation = 0x8006C558u; // u8 per body animation

// 0x80049F3C: when the current or the queued body animation blocks flame, clear the
// separate-tail-animation flag (a 32-bit `sw $zero`, although the decompilation types it as a
// byte); otherwise count one more flameable frame. v0 exits holding the nonzero table byte on the
// clearing paths and the new counter otherwise.
void updateFlameTailLock(Core *c) {
  const std::uint32_t bodyBlocked = c->mem_r8(kFlameBlockedInAnimation + c->mem_r8(kBodyAnimation));
  const std::uint32_t blocked =
      bodyBlocked != 0 ? bodyBlocked
                       : c->mem_r8(kFlameBlockedInAnimation + c->mem_r8(kNextBodyAnimation));
  if (blocked != 0) {
    c->mem_w32(kSeparateTailAnimation, 0);
    c->r[2] = blocked;
    return;
  }
  const std::uint32_t flameableFrames = c->mem_r32(kFlameableFrames) + 1u;
  c->mem_w32(kFlameableFrames, flameableFrames);
  c->r[2] = flameableFrames;
}

// 0x8003CB24: add the delta time (a0) to the 16-step body frame progress; when the low byte reaches
// 16, subtract 16, promote the queued body animation and frame to current, and advance the queued
// frame, wrapping it to the animation's start frame once it reaches the end frame. The progress
// store is the delay slot of the wrap test, so the un-wrapped sum is written on both paths. v0
// exits holding the 0/1 test of the low byte, or the start frame it wraps to; v1 holds the
// un-wrapped sum, or the queued frame, or the end frame it was compared against.
void advanceBodyAnimation(Core *c) {
  const std::uint32_t sum = c->mem_r8(kBodyFrameProgress) + c->r[4];
  const std::uint32_t belowWrap = (sum & 0xFFu) < 0x10u ? 1u : 0u;
  c->r[2] = belowWrap;
  c->r[3] = sum;
  c->mem_w8(kBodyFrameProgress, static_cast<std::uint8_t>(sum));
  if (belowWrap != 0) {
    return;
  }
  const std::uint32_t remaining = sum - 0x10u;
  c->r[2] = remaining;
  c->mem_w8(kBodyFrameProgress, static_cast<std::uint8_t>(remaining));
  const std::uint32_t queuedFrame = c->mem_r8(kNextBodyAnimationFrame);
  const std::uint32_t queuedAnimation = c->mem_r8(kNextBodyAnimation);
  const std::uint32_t advancedFrame = queuedFrame + 1u;
  c->mem_w8(kBodyAnimation, static_cast<std::uint8_t>(queuedAnimation));
  c->mem_w8(kBodyAnimationFrame, static_cast<std::uint8_t>(queuedFrame));
  c->mem_w8(kNextBodyAnimationFrame, static_cast<std::uint8_t>(advancedFrame));
  const std::uint32_t details = kAnimationDetails + queuedAnimation * kAnimationEntryBytes;
  const std::uint32_t endFrame = c->mem_r8(details + 1u);
  const std::uint32_t withinAnimation = (advancedFrame & 0xFFu) < endFrame ? 1u : 0u;
  c->r[2] = withinAnimation;
  c->r[3] = endFrame;
  if (withinAnimation == 0) {
    const std::uint32_t startFrame = c->mem_r8(details);
    c->mem_w8(kNextBodyAnimationFrame, static_cast<std::uint8_t>(startFrame));
    c->r[2] = startFrame;
  }
}

} // namespace

void registerPlayerAnimationOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80049F3Cu, "update_flame_tail_lock", updateFlameTailLock);
  spyro::installNativeOverride(core, 0x8003CB24u, "advance_body_animation", advanceBodyAnimation);
}

} // namespace spyro1::native
