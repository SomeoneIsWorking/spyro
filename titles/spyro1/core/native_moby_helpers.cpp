#include "native_moby_helpers.h"

#include "guest_call.h"
#include "guest_globals.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

using spyro::guest::kModelSoundTables;

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

// The moby fields this body touches, at the offsets the measured MOBY_FIELDS list records for
// asm/42CC4.s. The two flag bytes are cleared here but are named by
// nothing this repository has measured — that list names +0x40 and +0x42 and stops there — so they
// carry their offset instead of a field name this port has not established.
constexpr std::uint32_t kMobyCollisionGroup = 0x08u;
constexpr std::uint32_t kMobyCollisionRegion = 0x34u;
constexpr std::uint32_t kMobyFlagByteAt41 = 0x41u;
constexpr std::uint32_t kMobyFlagByteAt4B = 0x4bu;
constexpr std::uint32_t kNoCollisionRegion = 0xffffu;

// ── 0x800529CC — put a moby in the "collides with nothing yet" state: the collision group word at
//     +0x08 to zero, the collision region halfword at +0x34 to 0xFFFF, and the flag bytes at +0x41
//     and +0x4B cleared. Both callers run it on a moby that has just been allocated and before its
//     spawn position is copied in — the `jal` at 0x80054658, whose delay slot `move $a0,$s3` is
//     what supplies the moby, and the `jal` at 0x800141B4 — so a0 is the only input. The halfword
//     store is the `jr $ra` delay slot and its value is the `addi $at,$zero,-1` before it, so $at
//     exits holding 0xFFFFFFFF; v0 and v1 are never written and carry in from the caller.
void clearMobyCollisionState(Core *c) {
  const std::uint32_t moby = c->r[4];
  c->mem_w8(moby + kMobyFlagByteAt4B, 0);
  c->mem_w8(moby + kMobyFlagByteAt41, 0);
  c->mem_w32(moby + kMobyCollisionGroup, 0);
  c->r[1] = 0xffffffffu; // at — the immediate the delay slot's halfword store consumes
  c->mem_w16(moby + kMobyCollisionRegion, static_cast<std::uint16_t>(kNoCollisionRegion));
}

constexpr std::uint32_t kMobyPosition = 0x0cu;
constexpr std::uint32_t kMobyPositionZ = 0x14u;
constexpr std::uint32_t kMobyModelClass = 0x36u;
constexpr std::uint32_t kMobyAnimationFrame = 0x46u;
constexpr std::uint32_t kSoundReferenceByte = 4u;
constexpr std::uint32_t kPositionalSound = 8u;
constexpr std::uint32_t kStopEverySound = 1u;
constexpr std::uint32_t kGroundProbeHeight = 0x5dcu;
constexpr std::uint32_t kGroundProbeRadius = 0x1000u;
constexpr std::uint32_t kGroundProbeFrame = 0x18u;
constexpr std::uint32_t kFifthArgumentSlot = 0x10u;
constexpr std::uint32_t kLagThreeFields = 3u;
constexpr std::uint32_t kLagFourFields = 4u;

// The guest callees the bodies below call, each named by the `jal` that reaches it. Every one is a
// code address, so it is the 26-bit field of that instruction rather than a `lui`+immediate pair
// the retail body assembles, and an address audit re-derives it from the `jal` itself.
//
// kFindGroundHeightBelow is the highest world or actor triangle below a point, searched within a1.
// It snapshots its CALLER's s0..s7, gp, sp, fp and ra into RAM at 0x3C017DD8 (the KUSEG mirror of
// the guest's 0x80017DD8) before it reads a0/a1, so an override that enters it must present
// retail's sp, ra and s0 or it writes two words no other caller writes — which is why the return
// address it records is behaviour too, and is named beside it.
constexpr std::uint32_t kFindGroundHeightBelow = 0x8004D5ECu;       // `jal` at 0x80038360
constexpr std::uint32_t kFindGroundHeightBelowReturn = 0x80038368u; // the `jal`'s own pc + 8
// Stop the sounds a moby owns. Already an override in its own right (native_sound_position), so
// this is the same entry from the caller's side.
constexpr std::uint32_t kStopMobySounds = 0x800562A4u; // `jal` at 0x8003856C
// Start one sound, 3D (flag 8) or 2D, on a named channel.
constexpr std::uint32_t kPlaySound = 0x80055A78u; // `jal` at 0x8003859C
// The shortest delta between two 8-bit angles, |a0 - a1| wrapped into [0, 0x80]. A leaf — a0 and a1
// are all it reads.
constexpr std::uint32_t kAnimationAngleDelta = 0x80017908u; // `jal` at 0x80038F14
// The frame an animation reaches from its current frame, a1, by a2, with a3 the wrap threshold. A
// leaf — a0..a3 are all it reads.
constexpr std::uint32_t kAdvanceAnimationFrame = 0x800179F0u; // `jal` at 0x80038F70 and 0x80038F94

// ── 0x80038340 — the ground height under a moby, asked for from 1500 units above it.
//     a0 = &moby->m_Position, a1 = 4096. The z bump is STORED before the query runs — the store is
//     the `jal`'s delay slot — and the z the restore leaves is what v1 exits holding, reloaded from
//     memory rather than from the pre-call word, so a query that moved z is reflected. v0 is the
//     query's own return, which the body never touches again.
//     The frame, s0 and ra are behaviour, not bookkeeping: the query records its CALLER's s0..s7,
//     gp, sp, fp and ra into RAM at 0x3C017DD8 before it reads a0/a1, so an override that entered
//     it with the host's sp and ra wrote words nobody else writes. The body therefore stands up
//     retail's 0x18-byte frame, s0 = the moby, and ra = the instruction after the `jal` for the
//     duration of the call. The frame's own two stores are inside the 8 KiB of dead stack the
//     differential ignores, and the callee reads the caller's registers rather than the caller's
//     frame, so they are not reproduced.
void mobyGroundHeight(Core *c) {
  const std::uint32_t moby = c->r[4];
  const std::uint32_t callerSp = c->r[29];
  const std::uint32_t callerS0 = c->r[16];
  const std::uint32_t callerRa = c->r[31];
  c->r[29] = callerSp - kGroundProbeFrame;
  c->r[16] = moby; // `move $s0, $a0`
  c->r[31] = kFindGroundHeightBelowReturn;
  c->mem_w32(moby + kMobyPositionZ, c->mem_r32(moby + kMobyPositionZ) + kGroundProbeHeight);
  psx::cpu::callGuestNow(
      *c, "moby_ground_height", kFindGroundHeightBelow, moby + kMobyPosition, kGroundProbeRadius);
  const std::uint32_t restored = c->mem_r32(moby + kMobyPositionZ) - kGroundProbeHeight;
  c->mem_w32(moby + kMobyPositionZ, restored);
  c->r[16] = callerS0;
  c->r[31] = callerRa;
  c->r[29] = callerSp;
  c->r[3] = restored;
}

// The `lbu 4(v0)` one level above the model's sound table: the class is a SIGNED halfword, so a
// negative one indexes downwards, and the entry itself is taken from the model's table plus the
// sound index. Both the channel-present and channel-absent paths re-run these four instructions,
// the second one after the stop call, so the class is read again rather than carried over.
std::uint32_t
mobySoundReference(Core *c, const std::uint32_t moby, const std::uint32_t soundIndex) {
  const std::uint32_t modelClass = static_cast<std::uint32_t>(c->mem_r16s(moby + kMobyModelClass));
  const std::uint32_t soundTable = c->mem_r32(kModelSoundTables + (modelClass << 2));
  return c->mem_r8(soundTable + soundIndex + kSoundReferenceByte);
}

// ── 0x8003851C — play one of a moby model's sounds positionally, on a caller-named channel.
//     With a channel nothing else happens; without one the moby's own sounds are stopped FIRST and
//     the play goes to its own channel at +0x54. Both paths end in the same `jal`, so v0 and v1 are
//     whatever it leaves — the body writes neither register itself, and the "with a channel" path
//     never ran the stop call whose v1 would otherwise still be there.
void playMobySound(Core *c) {
  const std::uint32_t moby = c->r[4];
  const std::uint32_t soundIndex = c->r[5];
  const std::uint32_t channel = c->r[6];
  if (channel != 0u) {
    psx::cpu::callGuestNow(*c,
                           "play_moby_sound",
                           kPlaySound,
                           mobySoundReference(c, moby, soundIndex),
                           moby,
                           kPositionalSound,
                           channel);
    return;
  }
  psx::cpu::callGuestNow(*c, "play_moby_sound", kStopMobySounds, moby, kStopEverySound);
  psx::cpu::callGuestNow(*c,
                         "play_moby_sound",
                         kPlaySound,
                         mobySoundReference(c, moby, soundIndex),
                         moby,
                         kPositionalSound,
                         moby + kSoundChannel);
}

// ── kAdvanceAnimationFrame's caller half: ask for the new frame, then store the byte
//     it returns. The frame argument is the same word in both halves, and the fourth argument is
//     that word halved (`sra 1` then +1), which is arithmetic, so it rounds towards minus infinity.
void setMobyAnimationFrame(Core *c,
                           const std::uint32_t animation,
                           const std::uint32_t moby,
                           const std::uint32_t frame) {
  const std::uint32_t half = static_cast<std::uint32_t>(static_cast<std::int32_t>(frame) >> 1);
  psx::cpu::callGuestNow(*c,
                         "advance_moby_animation",
                         kAdvanceAnimationFrame,
                         animation,
                         c->mem_r8(moby + kMobyAnimationFrame),
                         frame,
                         half + 1u);
  c->mem_w8(moby + kMobyAnimationFrame, static_cast<std::uint8_t>(c->r[2]));
}

// ── 0x80038EE0 — advance a moby's animation frame, stretched by the frame-lag global, and report
//     whether the animation has run past its length. Lag 3 scales the step by 1.5 and lag 4 by 2,
//     anything else leaves it alone; a signed `slt` then clamps the step down to the length
//     guest kAnimationAngleDelta reports, and a second one against the caller's limit
//     decides the exit value: 1 when the length is BELOW the limit, 0 when it has reached it. The
//     fifth argument arrives on the stack at entry sp+0x10 and only decides whether the wrap is
//     stored, so v0 is 1 either way on the short arm. v1 holds the lag global, set after the length
//     call and then overwritten by the frame call wherever that one runs.
void advanceMobyAnimation(Core *c) {
  const std::uint32_t moby = c->r[4];
  const std::uint32_t animation = c->r[5];
  const std::uint32_t step = c->r[6];
  const std::uint32_t limit = c->r[7];
  const std::uint32_t wrapRequested = c->mem_r32(c->r[29] + kFifthArgumentSlot);

  psx::cpu::callGuestNow(*c,
                         "advance_moby_animation",
                         kAnimationAngleDelta,
                         animation,
                         c->mem_r8(moby + kMobyAnimationFrame));
  const std::uint32_t length = c->r[2];

  const std::uint32_t lag = c->mem_r32(kDeltaTimeGlobal);
  c->r[3] = lag;
  std::uint32_t frame = step;
  if (lag == kLagThreeFields) {
    frame = step + static_cast<std::uint32_t>(static_cast<std::int32_t>(step) >> 1);
  } else if (lag == kLagFourFields) {
    frame = step << 1;
  }
  if (static_cast<std::int32_t>(length) < static_cast<std::int32_t>(frame)) {
    frame = length;
  }
  if (static_cast<std::int32_t>(length) >= static_cast<std::int32_t>(limit)) {
    setMobyAnimationFrame(c, animation, moby, frame);
    c->r[2] = 0;
    return;
  }
  if (wrapRequested != 0u) {
    setMobyAnimationFrame(c, animation, moby, frame);
  }
  c->r[2] = 1;
}

// ── 0x80037E98 — the moby interpolation check, whose retail body is EMPTY: `jr $ra` at 0x80037E98
//     and a `nop` in its delay slot, and no third instruction anywhere in the function. It writes
//     no register and no memory, so v0 and v1 carry in whatever the caller left and the moby
//     pointer it is handed is never read.
void mobyInterpolationCheck(Core *) {}

// The globals the body below reaches, each spelled the way external/spyro-1/asm/42CC4.s annotates
// its own `lui`/`addiu` pair for this very function, and each the full literal so
// an address audit decodes it back to those two instructions instead of trusting a
// comment.
constexpr std::uint32_t kMobyCollisionChain = 0x80075778u;
constexpr std::uint32_t kDynMobys = 0x80075890u;
constexpr std::uint32_t kDynMobyCount = 0x800756A4u;
constexpr std::uint32_t kMobyAllocPtr = 0x8007573Cu;
constexpr std::uint32_t kPropsAllocPtr = 0x80075930u;
// The moby fields it reads, and the two block strides it walks in. 0x48 is the mark byte the
// allocator at 0x800524C4 writes 0 to for a live moby, and 0x58 is the pool's own stride.
constexpr std::uint32_t kMobyChainLink = 0x04u;
constexpr std::uint32_t kMobyMark = 0x48u;
constexpr std::uint32_t kMobyPoolStride = 0x58u;
constexpr std::uint32_t kMobyPropsEntryStride = 0x18u;
// The three dead-moby marks, in the sign-extended form the body holds in $v0/$v1. The allocator
// gives a live moby 0, so every one of these is written to a moby this body is giving up. `sb`
// truncates all three to a byte.
constexpr std::uint32_t kMarkBelowPool = 0xFFFFFFFDu;
constexpr std::uint32_t kMarkReleased = 0xFFFFFFFEu;
constexpr std::uint32_t kMarkRunBoundary = 0xFFFFFFFFu;

// ── 0x80052568 — release one moby, the counterpart of the allocator at 0x800524C4. A non-negative
//     m_CollisionRegion unlinks the moby from that region's chain; a moby below the dynamic pool is
//     only marked -3 and returns, v1 untouched. Otherwise the count drops and the free list is
//     reopened at the moby: its own mark becomes -2, or, when the node above it already carries
//     the -1 run boundary, the run of -2 below it is walked and the top of that run marked -1
//     instead, extending the run. Every mark test is a sign-extended byte read in a delay slot that
//     also steps the walk, so v1 is -2 or -1 at each store. g_MobyAllocPtr rolls back to whichever
//     node was marked, and the 0x18-byte entries behind m_Props — each mark its last byte — repeat
//     the whole thing against g_PropsAllocPtr, walking up where the pool walk walks down. v0 exits
//     holding g_PropsAllocPtr's own address and v1 the signed difference that cursor tested.
void releaseMoby(Core *c) {
  const std::uint32_t moby = c->r[4];
  const std::uint32_t region = static_cast<std::uint32_t>(c->mem_r16s(moby + kMobyCollisionRegion));
  if (static_cast<std::int32_t>(region) >= 0) {
    std::uint32_t slot = c->mem_r32(kMobyCollisionChain) + (region << 2);
    while (c->mem_r32(slot) != moby) {
      slot = c->mem_r32(slot) + 4u;
    }
    c->mem_w32(slot, c->mem_r32(moby + kMobyChainLink));
  }

  c->r[2] = kMarkBelowPool;
  if (static_cast<std::int32_t>(c->mem_r32(kDynMobys) - moby) > 0) {
    c->mem_w8(moby + kMobyMark, static_cast<std::uint8_t>(kMarkBelowPool));
    return;
  }
  c->r[2] = c->mem_r32(kDynMobyCount) - 1u;
  c->mem_w32(kDynMobyCount, c->r[2]);

  std::uint32_t pool = moby;
  c->r[2] = static_cast<std::uint32_t>(c->mem_r8s(moby + kMobyPoolStride + kMobyMark));
  c->r[3] = kMarkReleased;
  if (c->r[2] != kMarkRunBoundary) {
    c->mem_w8(pool + kMobyMark, c->r[3]);
  } else {
    pool = moby - kMobyPoolStride;
    for (;;) {
      c->r[2] = static_cast<std::uint32_t>(c->mem_r8s(pool + kMobyMark));
      c->r[3] = kMarkReleased;
      pool -= kMobyPoolStride;
      if (c->r[2] != kMarkReleased) {
        break;
      }
    }
    pool += kMobyPoolStride;
    pool += kMobyPoolStride;
    c->r[3] = kMarkRunBoundary;
    c->mem_w8(pool + kMobyMark, c->r[3]);
  }
  c->r[3] = c->mem_r32(kMobyAllocPtr);
  if (static_cast<std::int32_t>(c->r[3] - pool) >= 0) {
    c->mem_w32(kMobyAllocPtr, pool);
  }

  const std::uint32_t props = c->mem_r32(moby);
  std::uint32_t entry = props + kMobyPropsEntryStride;
  c->r[2] = static_cast<std::uint32_t>(c->mem_r8s(props - 1u));
  c->r[3] = kMarkReleased;
  if (c->r[2] != kMarkRunBoundary) {
    c->mem_w8(entry - 1u, c->r[3]);
  } else {
    entry += kMobyPropsEntryStride;
    for (;;) {
      c->r[2] = static_cast<std::uint32_t>(c->mem_r8s(entry - 1u));
      c->r[3] = kMarkReleased;
      entry += kMobyPropsEntryStride;
      if (c->r[2] != kMarkReleased) {
        break;
      }
    }
    entry -= kMobyPropsEntryStride;
    entry -= kMobyPropsEntryStride;
    c->r[3] = kMarkRunBoundary;
    c->mem_w8(entry - 1u, c->r[3]);
  }
  c->r[2] = kPropsAllocPtr;
  c->r[3] = c->mem_r32(kPropsAllocPtr) - entry;
  if (static_cast<std::int32_t>(c->r[3]) <= 0) {
    c->mem_w32(kPropsAllocPtr, entry);
  }
}

} // namespace

void registerMobyHelperOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x80037E98u, "moby_interpolation_check", mobyInterpolationCheck);
  spyro::installNativeOverride(core, 0x80037F90u, "tick_moby_timer", tickMobyTimer);
  spyro::installNativeOverride(core, 0x80038340u, "moby_ground_height", mobyGroundHeight);
  spyro::installNativeOverride(core, 0x8003851Cu, "play_moby_sound", playMobySound);
  spyro::installNativeOverride(core, 0x80038EE0u, "advance_moby_animation", advanceMobyAnimation);
  spyro::installNativeOverride(core, 0x8003A720u, "reset_moby_defaults", resetMobyDefaults);
  spyro::installNativeOverride(
      core, 0x800529CCu, "clear_moby_collision_state", clearMobyCollisionState);
  spyro::installNativeOverride(core, 0x80052568u, "release_moby", releaseMoby);
}

} // namespace spyro1::native
