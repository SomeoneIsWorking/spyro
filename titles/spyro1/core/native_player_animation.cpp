#include "native_player_animation.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

constexpr std::uint32_t kBodyAnimation = 0x80078A70u;     // g_Spyro.m_bodyAnimation (u8)
constexpr std::uint32_t kNextBodyAnimation = 0x80078A71u; // g_Spyro.m_nextBodyAnimation (u8)
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

} // namespace

void registerPlayerAnimationOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80049F3Cu, "update_flame_tail_lock", updateFlameTailLock);
}

} // namespace spyro1::native
