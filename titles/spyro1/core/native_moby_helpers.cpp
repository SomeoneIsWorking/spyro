#include "native_moby_helpers.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

// ── 0x80037F90 — count a moby timer down by the frame delta global: a0 is the timer, a1 its width
//     in bytes (4, 2 or 1), and each width is one copy of load, `slt` the signed 32-bit delta
//     against the timer, then either zero the timer and return 2, return 1 because it was already
//     zero, or subtract the delta and return 0. v1 exits holding the loaded timer — sign-extended
//     for `lh`, zero-extended for `lbu` — and it is filled from a2 in a branch delay slot, so it is
//     set on both arms. A width that is none of the three jumps straight out with v0 = 1 and v1
//     untouched, which is the value the decomp's missing return actually produces.
constexpr std::uint32_t kDeltaTimeGlobal = 0x800756C4u;

void tickMobyTimer(Core *c) {
  const std::uint32_t timerAddr = c->r[4];
  const std::uint32_t timerWidth = c->r[5];
  if (timerWidth != 4u && timerWidth != 2u && timerWidth != 1u) {
    c->r[2] = 1;
    return;
  }
  const std::int32_t delta = static_cast<std::int32_t>(c->mem_r32(kDeltaTimeGlobal));
  if (timerWidth == 4u) {
    const std::uint32_t timer = c->mem_r32(timerAddr);
    c->r[3] = timer;
    if (delta < static_cast<std::int32_t>(timer)) {
      c->mem_w32(timerAddr, timer - static_cast<std::uint32_t>(delta));
      c->r[2] = 0;
    } else if (timer != 0u) {
      c->mem_w32(timerAddr, 0);
      c->r[2] = 2;
    } else {
      c->r[2] = 1;
    }
    return;
  }
  if (timerWidth == 2u) {
    const std::int32_t timer = c->mem_r16s(timerAddr);
    c->r[3] = static_cast<std::uint32_t>(timer);
    if (delta < timer) {
      c->mem_w16(timerAddr, static_cast<std::uint16_t>(timer - delta));
      c->r[2] = 0;
    } else if (timer != 0) {
      c->mem_w16(timerAddr, 0);
      c->r[2] = 2;
    } else {
      c->r[2] = 1;
    }
    return;
  }
  const std::int32_t timer = c->mem_r8(timerAddr);
  c->r[3] = static_cast<std::uint32_t>(timer);
  if (delta < timer) {
    c->mem_w8(timerAddr, static_cast<std::uint8_t>(timer - delta));
    c->r[2] = 0;
  } else if (timer != 0) {
    c->mem_w8(timerAddr, 0);
    c->r[2] = 2;
  } else {
    c->r[2] = 1;
  }
}

constexpr std::uint32_t kRenderRadius = 0x50u;
constexpr std::uint32_t kDepthOffset = 0x47u;
constexpr std::uint32_t kNextAnimationFrame = 0x3fu;
constexpr std::uint32_t kUpdateDistance = 0x52u;
constexpr std::uint32_t kPerFrameAnimationProgress = 0x41u;
constexpr std::uint32_t kPodIndex = 0x43u;
constexpr std::uint32_t kSectorIndex = 0x4au;
constexpr std::uint32_t kDropMobyIndex = 0x53u;
constexpr std::uint32_t kRenderer = 0x4bu;
constexpr std::uint32_t kSoundChannel = 0x54u;
constexpr std::uint32_t kShadowDistance = 0x38u;
constexpr std::uint32_t kSpecularMetalColour = 0x4cu;
constexpr std::uint32_t kRotationWords[] = {0x18u, 0x1cu};
constexpr std::uint32_t kZeroedByteFields[] = {
    0x3cu, 0x3du, 0x3eu, 0x40u, 0x44u, 0x45u, 0x46u, 0x48u, 0x49u, 0x57u};

// Resets the moby at $a0 to its spawn defaults: 20 byte stores, 3 word stores and 1 halfword
// store, with no branch and no load. Retail body at 0x8003A720. Its last word store sits in the
// $jr $ra delay slot, so the specular/metal colour word is written on the same path as the rest.
// The function is void in the source sense but still clobbers the two registers the differential
// compares: $v0 leaves holding 0x7F, the sound-channel byte stored last, and $v1 holding 0xFF,
// the unset index value it reuses for the pod, sector and drop stores.
void resetMobyDefaults(Core *c) {
  const std::uint32_t moby = c->r[4];

  c->mem_w8(moby + kRenderRadius, 16);
  c->mem_w8(moby + kDepthOffset, 4);
  c->mem_w8(moby + kNextAnimationFrame, 1);
  c->mem_w8(moby + kUpdateDistance, 0xff);
  c->mem_w8(moby + kPerFrameAnimationProgress, 32);
  c->mem_w8(moby + kPodIndex, 0xff);
  c->mem_w8(moby + kSectorIndex, 0xff);
  c->mem_w8(moby + kDropMobyIndex, 0xff);
  c->mem_w8(moby + kRenderer, 32);
  c->mem_w8(moby + kSoundChannel, 0x7f);
  c->mem_w16(moby + kShadowDistance, 0);
  c->mem_w32(moby + kSpecularMetalColour, 0);
  for (const std::uint32_t word : kRotationWords) {
    c->mem_w32(moby + word, 0);
  }
  for (const std::uint32_t field : kZeroedByteFields) {
    c->mem_w8(moby + field, 0);
  }

  c->r[2] = 0x7fu;
  c->r[3] = 0xffu;
}

} // namespace

void registerMobyHelperOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80037F90u, "tick_moby_timer", tickMobyTimer);
  spyro::installNativeOverride(core, 0x8003A720u, "reset_moby_defaults", resetMobyDefaults);
}

} // namespace spyro1::native
