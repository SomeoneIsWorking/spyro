#include "native_moby_allocator.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// g_DynMobyCount (moby.h:507), bumped once per call before either pool is touched.
constexpr std::uint32_t kDynMobyCount = 0x800756A4u;
// g_MobyAllocPtr (moby.h:498), the dynamic Moby pool's cursor. It walks UP through 0x58-byte Moby
// slots, and the byte at +0x48 of a slot is that pool's link byte; the same byte is the slot's
// spawn/alive flag, measured live below 0x80.
constexpr std::uint32_t kMobyAllocCursor = 0x8007573Cu;
constexpr std::uint32_t kMobySlotStride = 0x58u;
constexpr std::uint32_t kMobyLinkByte = 0x48u;
// g_PropsAllocPtr (moby.h:501), the props pool's cursor, which walks DOWNWARD. Every dynamic Moby
// gets 24 bytes of props space (loaders.c:650: "Every dynamic Moby gets 24 bytes of props space"),
// and a cursor names the byte just past the record it last took, so a record's own link byte is the
// one just BELOW the cursor — loaders.c:646 opens the pool with `*(data - 1) = 0xFF`, "Terminate
// the props". The record this pool takes is published into Moby::m_Props (moby.h:44), the +0x00
// word.
constexpr std::uint32_t kPropsAllocCursor = 0x80075930u;
constexpr std::uint32_t kPropsStride = 0x18u;
constexpr std::uint32_t kPropsLinkByte = 1u;
constexpr std::uint32_t kMobyPropsPointer = 0x00u;
// Both pools test their link byte SIGN-EXTENDED against -1, so 0xFF is the one value that skips the
// walk: every other marker from 0x80 up still stops a walk, it just does not take this arm.
constexpr std::int32_t kTakenLinkMarker = -1;

// ── 0x800524C4 — allocate one dynamic Moby, and the props record published into its m_Props word.
//     g_DynMobyCount += 1 ; v0 = g_MobyAllocPtr
//     a0 = [v0+0x48] ; [v0+0x48] = 0 ; a0 == -1 ? [v0+0x58+0x48] = 0xFF : walk v0+0x58 upward
//     g_MobyAllocPtr = v1 ; v1 = g_PropsAllocPtr
//     a1 = [v1-1] ; [v1-1] = 0 ; a1 == -1 ? [v1-0x19] = 0xFF : walk v1-0x18 downward
//     v1 -= 0x18 ; [v0] = v1 ; g_PropsAllocPtr = a0
// BOTH walks move their cursor in the DELAY SLOT of the branch that retests them — the Moby pool's
// +0x58 and the props pool's -0x18 — and each walk's exit undoes that move with its own `addiu`, so
// a walk ends ON the record whose link byte it read rather than one stride past it. The `while` and
// the `for` below advance inside their own bodies and land on the same cursor, and each -1 arm
// writes the marker into the record that walk would have read first. Both tests compare the byte
// SIGN-EXTENDED, which is why 0x80..0xFE stop a walk without taking the -1 arm.
//
// v0 exits as the Moby the pool cursor NAMED on entry, not the cursor the walk published, and v1 as
// one stride below the props cursor's own loaded value on BOTH arms of its test. at exits as
// g_PropsAllocPtr, a0 as the published props cursor, and a1 as the link byte the walk stopped on
// (-1 on the arm that never walks).
void allocateDynamicMoby(Core *c) {
  c->mem_w32(kDynMobyCount, c->mem_r32(kDynMobyCount) + 1u);

  const std::uint32_t moby = c->mem_r32(kMobyAllocCursor);
  const std::int32_t link = static_cast<std::int32_t>(c->mem_r8s(moby + kMobyLinkByte));
  c->mem_w8(moby + kMobyLinkByte, 0u);
  std::uint32_t slot = moby + kMobySlotStride;
  if (link == kTakenLinkMarker) {
    c->mem_w8(slot + kMobyLinkByte, static_cast<std::uint8_t>(kTakenLinkMarker));
  } else {
    while (static_cast<std::int32_t>(c->mem_r8s(slot + kMobyLinkByte)) >= 0) {
      slot += kMobySlotStride;
    }
  }
  c->mem_w32(kMobyAllocCursor, slot);

  const std::uint32_t head = c->mem_r32(kPropsAllocCursor);
  std::int32_t propsLink = static_cast<std::int32_t>(c->mem_r8s(head - kPropsLinkByte));
  c->mem_w8(head - kPropsLinkByte, 0u);
  std::uint32_t props = head - kPropsStride;
  if (propsLink == kTakenLinkMarker) {
    c->mem_w8(props - kPropsLinkByte, static_cast<std::uint8_t>(kTakenLinkMarker));
  } else {
    for (;;) {
      propsLink = static_cast<std::int32_t>(c->mem_r8s(props - kPropsLinkByte));
      props -= kPropsStride;
      if (propsLink < 0) {
        break;
      }
    }
    props += kPropsStride;
  }
  c->r[3] = head - kPropsStride;
  c->mem_w32(moby + kMobyPropsPointer, c->r[3]);
  c->mem_w32(kPropsAllocCursor, props);
  c->r[2] = moby;
  c->r[1] = kPropsAllocCursor;
  c->r[4] = props;
  c->r[5] = static_cast<std::uint32_t>(propsLink);
}

} // namespace

void registerMobyAllocatorOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x800524C4u, "allocate_dynamic_moby", allocateDynamicMoby);
}

} // namespace spyro1::native
