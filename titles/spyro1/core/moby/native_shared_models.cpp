#include "native_shared_models.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// g_WadHeader.m_PETE.m_Length and .m_Offset (external/spyro-1/include/wad.h), g_CdState.m_WadSector
// (cd.h) and _stacksize: each is the `lui` plus immediate pair the retail body builds.
constexpr std::uint32_t kPeteWadLength = 0x8007A714u;
constexpr std::uint32_t kPeteWadOffset = 0x8007A710u;
constexpr std::uint32_t kWadSector = 0x80076B90u;
constexpr std::uint32_t kStackSize = 0x800755A4u;
// g_Buffers.m_SharedAnimations (buffers.h, the tenth word of the struct) and g_Models[MODEL_COUNT]
// (moby.h), the two globals the body publishes into.
constexpr std::uint32_t kSharedAnimations = 0x800785FCu;
constexpr std::uint32_t kModels = 0x80076378u;
// The `lui $s1, 0x8020` every pointer is counted down from, and the offset the model block starts
// at inside the PETE image — the shared animations are relocated to [peteData + 0x800).
constexpr std::uint32_t kRamTop = 0x80200000u;
constexpr std::uint32_t kModelBlockBase = 0x800u;
constexpr std::uint32_t kModelTableStride = 8u;
constexpr std::uint32_t kModelCount = 64u;

// The guest helpers the body calls, each the target of a `jal` inside it.
constexpr std::uint32_t kCdLoadSync = 0x80016500u;             // `jal` at 0x8005B83C
constexpr std::uint32_t kMemcpy = 0x80016958u;                 // `jal` at 0x8005B868
constexpr std::uint32_t kPatchMobyModelPointers = 0x800133E0u; // `jal` at 0x8005B890
constexpr std::uint32_t kAllocateBuffers = 0x8005B6F8u;        // `jal` at 0x8005B8B8

constexpr char kLoadSharedModels[] = "load_shared_models";

// The retail body's own 0x30-byte frame. The words below the saved registers are not the prologue
// for show: CDLoadSync takes its FIFTH argument at 0x10($sp), which the port's CD loader reads as
// the request token, so the frame and the `addiu $v0,zero,0x258` store that fills that slot are
// load bearing. The epilogue's five reloads are reproduced for the same reason.
constexpr std::uint32_t kFrameBytes = 0x30u;
constexpr std::uint32_t kCdRetryFramesSlot = 0x10u;
constexpr std::uint32_t kSavedS0 = 0x18u;
constexpr std::uint32_t kSavedS1 = 0x1Cu;
constexpr std::uint32_t kSavedS2 = 0x20u;
constexpr std::uint32_t kSavedS3 = 0x24u;
constexpr std::uint32_t kSavedRa = 0x28u;
constexpr std::uint32_t kCdRetryFrames = 600u;

// 0x8005B7D8 — load PETE.WAD out of WAD.WAD below the bottom of the stack, relocate the shared
// animation block it carries to the lowest address that still clears the stack, then walk the
// 64-entry model table at the head of the image: for each entry with a non-zero offset, patch that
// model's moby pointers in place and register the result in g_Models, and finally lay out the draw
// pools.
//
// Two things the shape does not show. The loop stops on a ZERO offset BEFORE its 64-entry cap, and
// `addiu $s0,$s0,8` is the bnez delay slot, so the table cursor advances on the exit iteration too
// — which is why the bound is tested after the store rather than around the loop head. And v0/v1
// leave on whatever AllocateBuffers(0) leaves, on every path, because that call is the last thing
// the body does; the native owner of that address supplies both, so the override does not invent
// them.
void loadSharedModels(Core *c) {
  const std::uint32_t entrySp = c->r[29];
  c->r[29] -= kFrameBytes;
  c->mem_w32(c->r[29] + kSavedS0, c->r[16]);
  c->mem_w32(c->r[29] + kSavedS1, c->r[17]);
  c->mem_w32(c->r[29] + kSavedS2, c->r[18]);
  c->mem_w32(c->r[29] + kSavedS3, c->r[19]);
  c->mem_w32(c->r[29] + kSavedRa, c->r[31]);
  c->mem_w32(c->r[29] + kCdRetryFramesSlot, kCdRetryFrames);

  const std::uint32_t length = c->mem_r32(kPeteWadLength);
  const std::uint32_t stackSize = c->mem_r32(kStackSize);
  // The image lands below the stack and below where the shared animations will end up, so the
  // length is subtracted twice.
  const std::uint32_t peteData = kRamTop - length - length - stackSize;
  psx::cpu::callGuestNow(*c,
                         kLoadSharedModels,
                         kCdLoadSync,
                         c->mem_r32(kWadSector),
                         peteData,
                         length,
                         c->mem_r32(kPeteWadOffset));

  const std::uint32_t dataSize = c->mem_r32(peteData);
  const std::uint32_t shared = kRamTop - stackSize - dataSize;
  c->mem_w32(kSharedAnimations, shared);
  psx::cpu::callGuestNow(
      *c, kLoadSharedModels, kMemcpy, shared, peteData + kModelBlockBase, dataSize);

  std::uint32_t table = peteData;
  std::uint32_t registered = 0;
  for (;;) {
    const std::uint32_t offset = c->mem_r32(table + 4u);
    if (offset == 0u) {
      break;
    }
    registered += 1u;
    psx::cpu::callGuestNow(*c,
                           kLoadSharedModels,
                           kPatchMobyModelPointers,
                           c->mem_r32(kSharedAnimations) + offset - kModelBlockBase);
    c->mem_w32(kModels + (c->mem_r32(table + 8u) << 2), c->r[2]);
    table += kModelTableStride;
    if (registered >= kModelCount) {
      break;
    }
  }

  psx::cpu::callGuestNow(*c, kLoadSharedModels, kAllocateBuffers, 0u);

  c->r[31] = c->mem_r32(c->r[29] + kSavedRa);
  c->r[19] = c->mem_r32(c->r[29] + kSavedS3);
  c->r[18] = c->mem_r32(c->r[29] + kSavedS2);
  c->r[17] = c->mem_r32(c->r[29] + kSavedS1);
  c->r[16] = c->mem_r32(c->r[29] + kSavedS0);
  c->r[29] = entrySp;
}

} // namespace

void registerSharedModelOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x8005B7D8u, "load_shared_models", loadSharedModels);
}

} // namespace spyro1::native
