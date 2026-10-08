#include "native_moby_lists.h"

#include "guest_call.h"
#include "guest_globals.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

using spyro::guest::kLevelMobys;
using spyro::guest::kModelSoundTables;
using spyro::guest::kRegisterSpillArea;

constexpr std::uint32_t kRenderQueue = 0x8006FCF4u;
constexpr std::uint32_t kSignedQueue = kRenderQueue + 0x2200u;
constexpr std::uint32_t kZeroQueue = kRenderQueue + 0x2400u;
constexpr std::uint32_t kCategoryVisibility = 0x800771C8u;
constexpr std::uint32_t kMobyState = 0x48u;
constexpr std::uint32_t kMobyVisible = 0x51u;
constexpr std::uint32_t kMobyStride = 0x58u;
constexpr std::uint32_t kEndOfArray = 0xFFu;
constexpr std::uint32_t kAnyCategory = 0xFFu;

// ── 0x800521C0 — queue the level's mobys for the field passes. It walks the 0x58-stride array
//     behind g_LevelMobys and files every entry under its own 32-bit state word at +0x48, which
//     two different bytes of that word decide. The low byte is the walk control: a top bit set
//     there skips the entry, and 0xFF ends the walk. The top byte is the class, and 0 sends the
//     entry to the $a0 queue, a negative class to the $v1 queue, and 1..0x7F to the $v0 queue.
//     The third byte indexes a category table: a zero byte there un-queues the entry again and
//     clears its +0x51 visibility byte, while 0xFF skips the table. The delay slots do the pointer
//     work — the cursor advances in the test's slot and retreats in the $j's — and all three
//     terminator stores run in the $jr delay slot. v0 and v1 exit as the cursors published.
void buildMobyRenderQueues(Core *c) {
  std::uint32_t moby = c->mem_r32(kLevelMobys);
  std::uint32_t render = kRenderQueue;
  std::uint32_t signedQueue = kSignedQueue;
  std::uint32_t zeroQueue = kZeroQueue;
  for (;;) {
    const std::uint32_t state = c->mem_r32(moby + kMobyState);
    const std::uint32_t low = state & 0xFFu;
    if ((low & 0x80u) != 0u) {
      if (low == kEndOfArray) {
        break;
      }
    } else {
      const std::int32_t kind = static_cast<std::int32_t>(state) >> 24;
      std::uint32_t &cursor = kind == 0 ? zeroQueue : (kind < 0 ? signedQueue : render);
      c->mem_w32(cursor, moby);
      cursor += 4u;
      const std::uint32_t category = (state >> 16) & 0xFFu;
      if (category != kAnyCategory && c->mem_r8(kCategoryVisibility + category) == 0u) {
        c->mem_w8(moby + kMobyVisible, 0u);
        cursor -= 4u;
      }
    }
    moby += kMobyStride;
  }
  c->mem_w32(render, 0u);
  c->mem_w32(signedQueue, 0u);
  c->mem_w32(zeroQueue, 0u);
  c->r[2] = render;
  c->r[3] = signedQueue;
}

constexpr std::uint32_t kEventQueue = 0x8006FCF4u;
// The pass drains its own queue through the guest's own register-save area (kRegisterSpillArea),
// because that is where a guest callee already leaves the caller's ra for it to read back.
// Start one sound on a named channel; the `jal` at 0x80052490 is instruction word 0x0C01569E, so
// its target is that J field rather than a lui/addiu pair, and the address audit
// re-derives it from the `jal` itself.
constexpr std::uint32_t kDispatchSound = 0x80055A78u;
constexpr std::uint32_t kStagedScale = 0x1F800000u;
constexpr std::uint32_t kStagedScaleStep = kStagedScale + 1u;
constexpr std::uint32_t kStagedFlags = kStagedScale + 2u;
constexpr std::uint32_t kStagedSound = kStagedScale + 4u;
constexpr std::uint32_t kStagedNextSound = kStagedScale + 5u;
constexpr std::uint32_t kStagedFrame = kStagedScale + 6u;
constexpr std::uint32_t kStagedFrameCount = kStagedScale + 7u;
constexpr std::uint32_t kMobyClass = 0x36u;
constexpr std::uint32_t kMobyAnimation = 0x3Cu;
constexpr std::uint32_t kMobyBlend = 0x40u;
constexpr std::uint32_t kSequenceTable = 0x38u;
constexpr std::uint32_t kFollowOnSound = 0x0Cu;
constexpr std::uint32_t kSoundRow = 0x24u;
constexpr std::uint32_t kSoundRowStride = 8u;
constexpr std::uint32_t kMobySoundChannel = 0x54u;
constexpr std::uint32_t kEventStride = 8u;
constexpr std::uint32_t kSoundPriority = 8u;
constexpr std::uint32_t kScaleLimit = 64u;

// ── 0x800522C0 — the animation pass over one moby queue. Each moby's eight animation bytes are
//     staged in the scratchpad at 0x1F800000 as the 0x40 word (a scale, a shift step and the flags
//     byte) and the 0x3C word (the sounding id, the requested id, the frame and its counter), and
//     the scale plus the step shifted right by a1 decides which of two states the moby takes. Below
//     64 the pass only writes the scale back and clears the flags when a1 is not negative; at or
//     above it the pass rewrites the staged bytes through the moby's model, advancing the frame
//     counter or setting the "ran past the last frame" flag bit, and appends the moby and its next
//     sound id to a buffer that the loop after it drains through 0x80055A78. The delay slots carry
//     state a source-level reading misses: `sb $a0,($t8)` writes the scale before the flags byte is
//     even read on the far arm, `ori $v0,$v0,4` runs on the skip that jumps back to the walk, the
//     frame counter store runs whether or not the end frame was passed, and the follow-on sound
//     byte and the scaled scale are stored on both arms of their own tests. v0 exits as the
//     animation word on the near arm, the buffer cursor on the far one, the raw flags byte on a
//     moby the pass skips, and whatever the drain call last left once a moby queues a sound.
void updateMobyAnimationSounds(Core *c) {
  const std::int32_t shift = static_cast<std::int32_t>(c->r[5]);
  std::uint32_t cursor = kEventQueue;
  std::uint32_t v0 = c->r[2];
  std::uint32_t walk = c->r[4];
  for (;;) {
    const std::uint32_t moby = c->mem_r32(walk);
    walk += 4u;
    if (moby == 0u) {
      break;
    }
    c->mem_w32(kStagedScale, c->mem_r32(moby + kMobyBlend));
    v0 = c->mem_r32(moby + kMobyAnimation);
    c->mem_w32(kStagedSound, v0);
    const std::uint32_t stagedStep = c->mem_r8(kStagedScaleStep);
    const std::uint32_t scale = c->mem_r8(kStagedScale) + (stagedStep >> (shift & 0x1F));
    if (scale < kScaleLimit) {
      if (shift >= 0) {
        c->mem_w8(kStagedFlags, 0u);
      }
      c->mem_w8(kStagedScale, static_cast<std::uint8_t>(scale & 0x3Fu));
      c->mem_w32(moby + kMobyBlend, c->mem_r32(kStagedScale));
      c->mem_w32(moby + kMobyAnimation, c->mem_r32(kStagedSound));
      v0 = c->mem_r32(kStagedSound);
      continue;
    }
    const std::uint32_t sound = c->mem_r8(kStagedSound);
    const std::uint32_t nextSound = c->mem_r8(kStagedNextSound);
    const std::uint32_t frameCount = c->mem_r8(kStagedFrameCount);
    std::uint32_t model = 0u;
    std::uint32_t sequence = 0u;
    if (sound != nextSound) {
      c->mem_w8(kStagedScale, 0u);
      c->mem_w8(kStagedSound, static_cast<std::uint8_t>(nextSound));
      c->mem_w8(kStagedFrame, static_cast<std::uint8_t>(frameCount));
      model = c->mem_r32(kModelSoundTables + c->mem_r16(moby + kMobyClass) * 4u);
      sequence = c->mem_r32(model + nextSound * 4u + kSequenceTable);
      const std::uint32_t next = frameCount + 1u;
      c->mem_w8(kStagedFrameCount, static_cast<std::uint8_t>(next));
      const std::uint32_t follow = c->mem_r8(sequence + kFollowOnSound);
      c->mem_w8(kStagedFlags, 1u);
      c->mem_w8(kStagedScaleStep, static_cast<std::uint8_t>(follow));
      if (follow == 0u) {
        c->mem_w8(kStagedFrameCount, static_cast<std::uint8_t>(next - 1u));
      }
    } else {
      v0 = 0u;
      if (shift < 0) {
        v0 = static_cast<std::uint32_t>(static_cast<std::int32_t>(c->mem_r8s(kStagedFlags)));
        if (v0 != 0u) {
          const bool finished = (v0 & 2u) != 0u;
          v0 |= 4u;
          if (finished) {
            continue;
          }
        }
      }
      c->mem_w8(kStagedScale, static_cast<std::uint8_t>(scale & 0x3Fu));
      c->mem_w8(kStagedFrame, static_cast<std::uint8_t>(frameCount));
      model = c->mem_r32(kModelSoundTables + c->mem_r16(moby + kMobyClass) * 4u);
      sequence = c->mem_r32(model + nextSound * 4u + kSequenceTable);
      const std::uint32_t next = frameCount + 1u;
      v0 |= 1u;
      c->mem_w8(kStagedFlags, static_cast<std::uint8_t>(v0));
      if (static_cast<std::int32_t>(next - c->mem_r16(sequence)) < 0) {
        c->mem_w8(kStagedFrameCount, static_cast<std::uint8_t>(next));
      } else {
        c->mem_w8(kStagedFrameCount, 0u);
        v0 |= 2u;
        c->mem_w8(kStagedFlags, static_cast<std::uint8_t>(v0));
      }
    }
    c->mem_w32(moby + kMobyBlend, c->mem_r32(kStagedScale));
    c->mem_w32(moby + kMobyAnimation, c->mem_r32(kStagedSound));
    const std::int32_t soundRow = static_cast<std::int32_t>(
        c->mem_r8s(sequence + frameCount * kSoundRowStride + kSoundRow + 3u));
    v0 = cursor;
    c->mem_w32(cursor + 4u, moby);
    if (soundRow >= 0) {
      cursor += kEventStride;
      c->mem_w32(cursor - kEventStride,
                 c->mem_r8(model + static_cast<std::uint32_t>(soundRow) + 4u));
    }
  }
  c->r[2] = v0;
  if (cursor == kEventQueue) {
    return;
  }
  c->mem_w32(kRegisterSpillArea, kEventQueue);
  c->mem_w32(kRegisterSpillArea + 4u, cursor);
  c->mem_w32(kRegisterSpillArea + 8u, c->r[31]);
  std::uint32_t at = kEventQueue;
  for (;;) {
    const std::uint32_t sound = c->mem_r32(at);
    const std::uint32_t moby = c->mem_r32(at + 4u);
    at += kEventStride;
    c->mem_w32(kRegisterSpillArea, at);
    c->mem_w32(kRegisterSpillArea + 0x0Cu, moby);
    psx::cpu::callGuestNow(*c,
                           "moby_sound_event",
                           kDispatchSound,
                           sound,
                           moby,
                           kSoundPriority,
                           moby + kMobySoundChannel);
    at = c->mem_r32(kRegisterSpillArea);
    if (at == c->mem_r32(kRegisterSpillArea + 4u)) {
      break;
    }
  }
  c->r[31] = c->mem_r32(kRegisterSpillArea + 8u);
}

} // namespace

void registerMobyListOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x800521C0u, "build_moby_render_queues", buildMobyRenderQueues);
  psx::cpu::installNativeOverride(
      core, 0x800522C0u, "update_moby_animation_sounds", updateMobyAnimationSounds);
}

} // namespace spyro1::native
