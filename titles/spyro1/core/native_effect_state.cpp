#include "native_effect_state.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// The body forms its g_Spyro addresses from the rotation matrix at +0x34 (`lui $s0,0x8008; addiu
// $s0,$s0,-0x7574`), so each field is spelled as the offset from that one formed address — the
// displacement the guest itself uses. g_Spyro (spyro.h) is 0x80078A58.
constexpr std::uint32_t kSpyroRotation = 0x80078A8Cu;                // +0x34, m_RotationMatrix
constexpr std::uint32_t kSpyroPosition = kSpyroRotation - 0x34u;     // +0x00, m_Position
constexpr std::uint32_t kSpyroPositionZ = kSpyroRotation - 0x2Cu;    // +0x08
constexpr std::uint32_t kSpyroSortingDepth = kSpyroRotation - 0x0Du; // +0x27, m_sortingDepth
constexpr std::uint32_t kSpyroSurfaceBelow = kSpyroRotation + 0x6Cu; // +0xA0, m_surfaceBelowSpyro
constexpr std::uint32_t kSurfaceProximityState =
    kSpyroRotation + 0x13Cu;                             // +0x170, m_SurfaceProximityState
constexpr std::uint32_t kShadowFanOffsets = 0x8006E2A8u; // D_8006E2A8, the ring's eight fan offsets
constexpr std::uint32_t kHeightThresholdTable = 0x8006E9A4u; // D_8006E9A4, one word per level
constexpr std::uint32_t kShadowFrameBytes = 0x30u;
constexpr std::uint32_t kShadowSlots = 8u;
constexpr std::uint32_t kShadowStride = 12u;

// D_8007AA10 is the ground-shadow state game/render/field_shadow_recipe.cpp reads; its +0x20 ring
// index is the only address the body forms itself, so the rest are offsets from it.
constexpr std::uint32_t kShadowRingIndex = 0x8007AA30u;
constexpr std::uint32_t kShadowHeights = kShadowRingIndex - 0x20u;   // +0x00, eight signed bytes
constexpr std::uint32_t kShadowSlotFlags = kShadowRingIndex - 0x18u; // +0x08, eight bytes
constexpr std::uint32_t kShadowAnchor = kShadowRingIndex - 0x10u;    // +0x10, the anchor position
constexpr std::uint32_t kShadowReference = kShadowRingIndex - 0x08u; // +0x18, the reference height
constexpr std::uint32_t kShadowBias = kShadowRingIndex - 0x04u;      // +0x1C
constexpr std::uint32_t kShadowSuppress = kShadowRingIndex + 0x04u;  // +0x24, non-zero hides it

constexpr std::uint32_t kShadowBlendLimit = 0x7Fu;
constexpr std::uint32_t kShadowNearBand = 0x80u;
constexpr std::int32_t kShadowDropRange = 0x365;
constexpr std::int32_t kShadowLiftScale = 194;

constexpr std::uint32_t kLevelIndex = 0x80075964u; // g_LevelIndex

// The registered entry, named so the guest call site inside the body can be spelled as an offset
// from it. A `jal` returns to its own pc + 8, and callGuestNow returns through the $ra its caller
// left, so func_8004D5EC — which spills a frame before reading anything — must see the return
// address the guest's own call site would have installed.
constexpr std::uint32_t kUpdateGroundShadow = 0x80049FACu;
// The `jal` at 0x8004A118 returns to +0x174.
constexpr std::uint32_t kSurfaceProbeReturn = kUpdateGroundShadow + 0x174u;

// The callees, each named by the `jal` at the call site the comment gives. A callee is the 26-bit
// field of that instruction rather than a `lui`+immediate pair, and tools/override_constants.py
// re-derives it from the `jal` itself.
constexpr std::uint32_t kRotate =
    0x80017048u; // `jal` at 0x80049FE4 and 0x8004A0F0, VecRotateByMatrix
constexpr std::uint32_t kVecAdd = 0x80017758u;  // `jal` at 0x80049FF4 and 0x8004A100
constexpr std::uint32_t kVecCopy = 0x80017700u; // `jal` at 0x8004A004
// `jal` at 0x8004A118, func_8004D5EC
constexpr std::uint32_t kSurfaceProbe = 0x8004D5ECu;

constexpr const char *kGroundShadow = "update_ground_shadow";

// 0x80049FAC — maintain Spyro's ground shadow: publish the anchor through his own rotation matrix,
// probe the surface under this ring slot, and record the signed lift and the flags the shadow
// recipe reads.
//
// The exit state is per path, and the paths differ in which register holds what:
//   * The ring index is advanced in the delay slot of the armed test, so it advances on BOTH paths,
//     and it is the base of the two byte stores at the end — which the body re-reads each time, so
//     it is read again here after the surface probe.
//   * When the surface is within 0x80 of the anchor, v0 exits as the scaled lift and v1 as the lift
//     byte's address; otherwise v0 exits as that address and v1 as the lift array itself.
//   * The entry's a0 is captured into $s2 before the first call clobbers $a0, and the armed test
//     reads that captured value, not the call's leftovers. $s0, $s1 and $s2 are what func_8004D5EC
//     spills before it reads anything, so all three are carried in the register file here.
void updateGroundShadow(Core *c) {
  const std::uint32_t frame = c->r[29] - kShadowFrameBytes;
  const std::uint32_t entryRa = c->r[31];
  const std::uint32_t entryS0 = c->r[16];
  const std::uint32_t entryS1 = c->r[17];
  const std::uint32_t entryS2 = c->r[18];
  const std::uint32_t armed = c->r[4];
  c->r[29] = frame;
  c->r[16] = kSpyroRotation;
  c->r[18] = armed;
  // The ring slot's own offset, dropped straight down before the first rotation.
  c->r[4] = kSpyroRotation;
  c->r[5] = frame + 0x10u;
  c->r[6] = frame + 0x10u;
  c->mem_w32(frame + 0x10u, 0);
  c->mem_w32(frame + 0x14u, 0);
  c->mem_w32(frame + 0x18u, 0u - 0x164u);
  psx::cpu::callGuestNow(*c, kGroundShadow, kRotate, c->r[4], c->r[5], c->r[6], c->r[7]);
  c->r[4] = frame + 0x10u;
  c->r[5] = frame + 0x10u;
  c->r[6] = kSpyroPosition;
  psx::cpu::callGuestNow(*c, kGroundShadow, kVecAdd, c->r[4], c->r[5], c->r[6], c->r[7]);
  c->r[4] = kShadowAnchor;
  c->r[5] = frame + 0x10u;
  psx::cpu::callGuestNow(*c, kGroundShadow, kVecCopy, c->r[4], c->r[5], c->r[6], c->r[7]);
  if (c->mem_r8(kSpyroSortingDepth) < kShadowBlendLimit) {
    c->mem_w8(kSpyroSortingDepth, 5);
  }
  const std::uint32_t reference = c->mem_r32(kSpyroSurfaceBelow);
  c->mem_w32(kShadowReference, reference);
  c->mem_w32(kShadowBias, 3);
  const std::int32_t drop =
      static_cast<std::int32_t>(c->mem_r32(kSpyroPositionZ)) - static_cast<std::int32_t>(reference);
  if (drop >= kShadowDropRange) {
    c->mem_w32(kShadowBias, 5);
  }
  const std::int32_t threshold =
      static_cast<std::int32_t>(c->mem_r32(kHeightThresholdTable + c->mem_r32(kLevelIndex) * 4u));
  const std::uint32_t proximity = c->mem_r32(kSurfaceProximityState);
  const std::uint32_t suppress =
      threshold >= static_cast<std::int32_t>(reference) || proximity != 0u ? 1u : 0u;
  c->mem_w32(kShadowSuppress, suppress);
  c->r[17] = kShadowRingIndex;
  const std::uint32_t slot = (c->mem_r32(kShadowRingIndex) + 1u) & (kShadowSlots - 1u);
  c->mem_w32(kShadowRingIndex, slot);
  if (armed != 0u) {
    c->r[4] = kSpyroRotation;
    c->r[5] = kShadowFanOffsets + slot * kShadowStride;
    c->r[6] = frame + 0x10u;
    psx::cpu::callGuestNow(*c, kGroundShadow, kRotate, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = frame + 0x10u;
    c->r[5] = frame + 0x10u;
    c->r[6] = kSpyroPosition;
    psx::cpu::callGuestNow(*c, kGroundShadow, kVecAdd, c->r[4], c->r[5], c->r[6], c->r[7]);
    c->r[4] = frame + 0x10u;
    c->r[5] = 0x400u;
    const std::uint32_t anchorZ = c->mem_r32(frame + 0x18u);
    c->mem_w32(frame + 0x18u, anchorZ + 0x200u);
    c->r[31] = kSurfaceProbeReturn;
    psx::cpu::callGuestNow(*c, kGroundShadow, kSurfaceProbe, c->r[4], c->r[5], c->r[6], c->r[7]);
    const std::int32_t surface = static_cast<std::int32_t>(c->r[2]);
    c->mem_w32(frame + 0x18u, anchorZ);
    const std::int32_t lift = static_cast<std::int32_t>(anchorZ) - surface;
    const bool near = (static_cast<std::uint32_t>(lift) + kShadowNearBand) < 0x100u;
    const std::uint32_t store = c->mem_r32(kShadowRingIndex);
    if (near) {
      c->mem_w8(kShadowSlotFlags + store, 0);
      std::int32_t scaled =
          (static_cast<std::int32_t>(c->mem_r32(kShadowReference)) - surface) * kShadowLiftScale;
      if (scaled < 0) {
        scaled += 0x1FF;
      }
      const std::uint32_t value = static_cast<std::uint32_t>(scaled >> 9);
      c->mem_w8(kShadowHeights + store, static_cast<std::uint8_t>(value));
      c->r[2] = value;
      c->r[3] = kShadowHeights + store;
    } else {
      c->mem_w8(kShadowSlotFlags + store, 1);
      c->mem_w8(kShadowHeights + store, 0);
      c->r[2] = kShadowHeights + store;
      c->r[3] = kShadowHeights;
    }
  } else {
    const std::uint32_t store = c->mem_r32(kShadowRingIndex);
    c->mem_w8(kShadowSlotFlags + store, 0);
    c->mem_w8(kShadowHeights + store, 0);
    c->r[2] = kShadowHeights + store;
    c->r[3] = kShadowHeights;
  }
  c->r[16] = entryS0;
  c->r[17] = entryS1;
  c->r[18] = entryS2;
  c->r[31] = entryRa;
  c->r[29] = frame + kShadowFrameBytes;
}

} // namespace

void registerEffectStateOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80049FACu, "update_ground_shadow", updateGroundShadow);
}

} // namespace spyro1::native
