#include "native_player_animation.h"

#include "guest_call.h"
#include "guest_globals.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

using spyro::guest::kPlayerState;

constexpr std::uint32_t kBodyAnimation = 0x80078A70u;          // g_Spyro.m_bodyAnimation (u8)
constexpr std::uint32_t kNextBodyAnimation = 0x80078A71u;      // g_Spyro.m_nextBodyAnimation (u8)
constexpr std::uint32_t kBodyAnimationFrame = 0x80078A76u;     // g_Spyro.m_bodyAnimationFrame (u8)
constexpr std::uint32_t kNextBodyAnimationFrame = 0x80078A77u; // g_Spyro.m_nextBodyAnimationFrame
constexpr std::uint32_t kBodyFrameProgress = 0x80078A7Cu;      // g_Spyro.m_bodyFrameProgress (u8)
constexpr std::uint32_t kAnimationDetails = 0x8006C4A0u; // spyro_AnimationDetails (4-byte stride)
constexpr std::uint32_t kAnimationEntryBytes = 4u;
// The two byte fields this file reads are 2 bytes apart: 0x80070000 - 0x3B5E and - 0x3B60.
constexpr std::uint32_t kAnimationTransitionLastFrameOffset = 2u; // m_TransitionLastFrame (u8)
constexpr std::uint32_t kLastAnimationState = 0x80078AB4u; // g_Spyro.m_lastAnimationState (32-bit)
constexpr std::uint32_t kStatePairTable = 0x8006BC84u;     // [lastAnimationState][state] (u8)
constexpr std::uint32_t kStatePairRowBytes = 45u;
// 0x8003CC38 builds this as `lui 0x8007; addiu -0x3B90`: the addiu immediate is sign-extended.
constexpr std::uint32_t kStateDefaultAnimation = 0x8006C470u; // indexed by m_State (u8)
constexpr std::uint32_t kRestartAtAnimationStart = 10u; // state-pair value taking the start frame
constexpr std::uint32_t kSeparateTailAnimation = 0x80078C40u;
constexpr std::uint32_t kTailAnimation = 0x80078A74u;
constexpr std::uint32_t kNextTailAnimation = 0x80078A75u;
constexpr std::uint32_t kTailAnimationFrame = 0x80078A7Au;
constexpr std::uint32_t kNextTailAnimationFrame = 0x80078A7Bu;
constexpr std::uint32_t kTailFrameProgress = 0x80078A7Eu;
// The three g_Spyro words beside m_seperateTailAnimation that the decompilation leaves unnamed,
// named here for the tail stepper's own use of them.
constexpr std::uint32_t kTailAnimationHold = 0x80078AC4u;
constexpr std::uint32_t kTailAnimationSpeed = 0x80078AC8u;
constexpr std::uint32_t kTailAnimationMode = 0x80078ACCu;
// 0x80049DFC, the tail stepper's own callee, from the `jal` at 0x80049F24.
constexpr std::uint32_t kAdvanceTailAnimationFrames = 0x80049DFCu;
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

// 0x8003CBB8: add the delta time (a0) to the 16-step body frame progress and, on the wrap, promote
// the queued body animation and frame to current and advance the queued frame; once that frame has
// passed the animation's transition-last frame, queue the state default animation instead and
// record the state the transition came from. The progress store is the delay slot of the wrap test,
// so the un-wrapped sum is written on both paths, and the state is recorded with a 32-bit `sw` of
// the 32-bit state word. v0 exits 0 on the two no-transition paths and 1 on the transition; v1
// exits the un-wrapped sum, the transition-last frame it was compared against, or the state word.
void advanceBodyAnimationWithTransitions(Core *c) {
  const std::uint32_t sum = c->mem_r8(kBodyFrameProgress) + c->r[4];
  const std::uint32_t belowWrap = (sum & 0xFFu) < 0x10u ? 1u : 0u;
  c->r[3] = sum;
  c->mem_w8(kBodyFrameProgress, static_cast<std::uint8_t>(sum));
  if (belowWrap != 0) {
    c->r[2] = 0;
    return;
  }
  const std::uint32_t remaining = sum - 0x10u;
  c->mem_w8(kBodyFrameProgress, static_cast<std::uint8_t>(remaining));
  const std::uint32_t queuedFrame = c->mem_r8(kNextBodyAnimationFrame);
  const std::uint32_t queuedAnimation = c->mem_r8(kNextBodyAnimation);
  const std::uint32_t advancedFrame = queuedFrame + 1u;
  c->mem_w8(kBodyAnimation, static_cast<std::uint8_t>(queuedAnimation));
  c->mem_w8(kBodyAnimationFrame, static_cast<std::uint8_t>(queuedFrame));
  c->mem_w8(kNextBodyAnimationFrame, static_cast<std::uint8_t>(advancedFrame));
  const std::uint32_t details = kAnimationDetails + queuedAnimation * kAnimationEntryBytes;
  const std::uint32_t transitionLastFrame =
      c->mem_r8(details + kAnimationTransitionLastFrameOffset);
  const std::uint32_t withinAnimation = (advancedFrame & 0xFFu) < transitionLastFrame ? 1u : 0u;
  if (withinAnimation != 0) {
    c->r[2] = 0;
    c->r[3] = transitionLastFrame;
    return;
  }
  const std::uint32_t state = c->mem_r32(kPlayerState);
  const std::uint32_t defaultAnimation = c->mem_r8(state + kStateDefaultAnimation);
  c->mem_w8(kNextBodyAnimation, static_cast<std::uint8_t>(defaultAnimation));
  const std::uint32_t lastAnimationState = c->mem_r32(kLastAnimationState);
  const std::uint32_t statePair =
      c->mem_r8(kStatePairTable + lastAnimationState * kStatePairRowBytes + state);
  if (statePair == kRestartAtAnimationStart) {
    const std::uint32_t restart = kAnimationDetails + defaultAnimation * kAnimationEntryBytes;
    c->mem_w8(kNextBodyAnimationFrame, static_cast<std::uint8_t>(c->mem_r8(restart)));
  } else {
    c->mem_w8(kNextBodyAnimationFrame, 1);
    c->mem_w8(kBodyFrameProgress, 4);
  }
  c->mem_w32(kLastAnimationState, state);
  c->r[2] = 1;
  c->r[3] = state;
}

// Spyro's head-look state: per axis, a target angle, the current angle and its velocity (words),
// plus one signed rotation byte per axis.
struct HeadLookAxis {
  std::uint32_t target;
  std::uint32_t current;
  std::uint32_t velocity;
  std::uint32_t rotation;
};
constexpr HeadLookAxis kHeadLookAxes[3] = {
    {0x80078BFCu, 0x80078C08u, 0x80078C14u, 0x80078A68u},
    {0x80078C00u, 0x80078C0Cu, 0x80078C18u, 0x80078A69u},
    {0x80078C04u, 0x80078C10u, 0x80078C1Cu, 0x80078A6Au},
};
constexpr std::uint32_t kAngleMask = 0xFFFu;
constexpr std::uint32_t kFullTurn = 0x1000u;
constexpr std::uint32_t kHalfTurn = 0x800u;

struct HeadLookStep {
  std::int32_t rotation; // the new angle >> 4, as stored in the rotation byte
  std::int32_t acceleration;
};

// One axis: wrap the 12-bit (target - current) delta to the shortest signed direction, integrate it
// into the velocity, advance the angle by velocity >> 6, and store the angle >> 4 as the rotation.
HeadLookStep stepHeadLookAxis(Core *c, const HeadLookAxis &axis) {
  const std::uint32_t current = c->mem_r32(axis.current);
  std::uint32_t delta = (c->mem_r32(axis.target) - current) & kAngleMask;
  if (delta > kHalfTurn) {
    delta -= kFullTurn;
  }
  const std::int32_t velocity = static_cast<std::int32_t>(c->mem_r32(axis.velocity));
  const std::int32_t acceleration =
      static_cast<std::int32_t>((delta << 7) - (static_cast<std::uint32_t>(velocity) << 4)) >> 6;
  const std::int32_t newVelocity = velocity + acceleration;
  c->mem_w32(axis.velocity, static_cast<std::uint32_t>(newVelocity));
  const std::int32_t angle = static_cast<std::int32_t>(current) + (newVelocity >> 6);
  c->mem_w32(axis.current, static_cast<std::uint32_t>(angle));
  c->mem_w8(axis.rotation, static_cast<std::uint8_t>(angle >> 4));
  return {angle >> 4, acceleration};
}

// 0x80049880: per-frame head-look smoothing over the three axes in order. v0 exits holding the z
// rotation (angle >> 4) and v1 the z acceleration term.
void smoothHeadLook(Core *c) {
  HeadLookStep last{};
  for (const HeadLookAxis &axis : kHeadLookAxes) {
    last = stepHeadLookAxis(c, axis);
  }
  c->r[2] = static_cast<std::uint32_t>(last.rotation);
  c->r[3] = static_cast<std::uint32_t>(last.acceleration);
}

// 0x80049E8C — the tail animation stepper: while the tail is not separately animated, copy the
// body's animation state onto the tail; otherwise advance the tail on its own speed.
//
// The three exits differ, and the register state is the whole reason to own this body:
//   * the hold word is loaded BEFORE the branch that tests it, so that early exit hands back the
//     hold word in v0 and leaves v1 exactly as the caller left it.
//   * the separate-tail test loads both words, so that exit hands back the mode in v0 and the
//     separate-tail word in v1.
//   * the copy path hands back the body animation and frame it has just copied, not the tail's.
void updateTailAnimation(Core *c) {
  const std::uint32_t hold = c->mem_r32(kTailAnimationHold);
  c->r[2] = hold;
  if (hold != 0u) {
    return;
  }
  const std::uint32_t separate = c->mem_r32(kSeparateTailAnimation);
  const std::uint32_t mode = c->mem_r32(kTailAnimationMode);
  c->r[3] = separate;
  c->r[2] = mode;
  if (separate != mode) {
    return;
  }
  if (separate != 0u) {
    c->r[4] = c->mem_r32(kTailAnimationSpeed);
    psx::cpu::callGuestNow(*c,
                           "update_tail_animation",
                           kAdvanceTailAnimationFrames,
                           c->r[4],
                           c->r[5],
                           c->r[6],
                           c->r[7]);
    return;
  }
  const std::uint32_t animation = c->mem_r8(kBodyAnimation);
  const std::uint32_t frame = c->mem_r8(kBodyAnimationFrame);
  const std::uint32_t nextAnimation = c->mem_r8(kNextBodyAnimation);
  const std::uint32_t nextFrame = c->mem_r8(kNextBodyAnimationFrame);
  const std::uint32_t progress = c->mem_r8(kBodyFrameProgress);
  c->mem_w8(kTailAnimation, static_cast<std::uint8_t>(animation));
  c->mem_w8(kTailAnimationFrame, static_cast<std::uint8_t>(frame));
  c->mem_w8(kNextTailAnimation, static_cast<std::uint8_t>(nextAnimation));
  c->mem_w8(kNextTailAnimationFrame, static_cast<std::uint8_t>(nextFrame));
  c->mem_w8(kTailFrameProgress, static_cast<std::uint8_t>(progress));
  c->r[2] = animation;
  c->r[3] = frame;
}

} // namespace

void registerPlayerAnimationOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8003CB24u, "advance_body_animation", advanceBodyAnimation);
  psx::cpu::installNativeOverride(core,
                                  0x8003CBB8u,
                                  "advance_body_animation_with_transitions",
                                  advanceBodyAnimationWithTransitions);
  psx::cpu::installNativeOverride(core, 0x80049880u, "smooth_head_look", smoothHeadLook);
  psx::cpu::installNativeOverride(core, 0x80049E8Cu, "update_tail_animation", updateTailAnimation);
  psx::cpu::installNativeOverride(core, 0x80049F3Cu, "update_flame_tail_lock", updateFlameTailLock);
}

} // namespace spyro1::native
