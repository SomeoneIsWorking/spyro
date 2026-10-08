#include "native_level_globals_table.h"

#include "native_execution.h"

#include <cstddef>
#include <cstdint>

namespace spyro1::native {
namespace {

// ── 0x8005A470 — the per-level globals fixup. It runs once per level entry, after that level's
//     overlay has been DMA'd into RAM, and points the engine's per-level globals at the overlay's
//     own routines (external/open-spyro documents it on the level-transition state machine; the
//     artisans-walk route calls it once, and once is all a level entry needs).
//     v1 = [0x8007596C] ; sltiu v0,v1,0x64 ; beqz v0,PUBLISH ; sll v0,v1,2 (delay slot)
//     v0 = [0x800113A4 + v1*4] ; jr v0        then 43 case bodies, each a run of `sw` only
//     PUBLISH: v0 = [0x800785D8] ; sw v0,0x800785DC(at) ; jr ra
// The whole body is a jump table over 100 level ids, so the table below is the function: the 43
// case bodies, decoded from the retail instructions, plus the epilogue, which the jump table also
// names. The table is written out here rather than read at run time, so the guest jump table at
// 0x800113A4 is no longer consulted — the level id indexes this one instead.
//
// THE TWO EXITS DIFFER ONLY IN ONE STORE. Most case bodies end by jumping to a shared exit that
// stores the value they left in v0 into the fifth primary slot, so those cases' last store is part
// of the case and belongs in its row; the rest jump past it. The last case has no jump at all and
// falls into that exit, and the 56 level ids the table points straight at the epilogue install
// nothing, so their row is empty.
//
// v0 carries the head of the bank out: the epilogue reloads g_pDrawBufA, so a level that installs
// anything publishes the value it just wrote and an unassigned level republishes whatever the
// previous one left. v1 carries the level id, and $at the epilogue's lui, 0x80080000.
constexpr std::uint32_t kActiveLevelId = 0x8007596Cu; // g_nActiveLevelId
constexpr std::uint32_t kDrawBufferA = 0x800785D8u;   // g_pDrawBufA, the head of the bank
constexpr std::uint32_t kDrawBufferB = 0x800785DCu;   // g_pDrawBufB, where the head is republished

// The globals the body writes, named where the decomp names them and by their position in the bank
// where it leaves them as _DAT_ globals.
constexpr std::uint32_t kOverlayInitHook = 0x800758CCu;      // g_pfnLevelOverlayInitHook
constexpr std::uint32_t kGamestate0EarlyHook = 0x80075734u;  // g_pfnGamestate0EarlyHook
constexpr std::uint32_t kOverlayParticleSpawn = 0x800758E4u; // g_pfnLevelOverlayParticleSpawn
constexpr std::uint32_t kGamestate0LateHook = 0x800756BCu;   // g_pfnGamestate0LateHook
constexpr std::uint32_t kPrimaryTailHook0 = 0x800757A0u;     // _DAT_800757a0
constexpr std::uint32_t kPrimaryTailHook1 = 0x800758A8u;     // _DAT_800758a8
constexpr std::uint32_t kPrimaryTailHook2 = 0x8007574Cu;     // _DAT_8007574c
constexpr std::uint32_t kTransitionDrawHook = 0x800758D8u;   // gamestate 0xC's draw hook
constexpr std::uint32_t kFallImpactCallback = 0x80075694u;   // g_pfnLevelFallImpactCallback
constexpr std::uint32_t kSecondaryHook0 = 0x800757A8u;       // _DAT_800757a8
constexpr std::uint32_t kGamestate7Handler = 0x800757C0u;    // g_pfnGamestate7Handler
constexpr std::uint32_t kGamestate7DrawHook = 0x8007567Cu;   // g_pfnGamestate7DrawHook
constexpr std::uint32_t kSecondaryHook1 = 0x800758C4u;       // _DAT_800758c4

// The two banks, each in the order the case bodies write them. Every case that installs anything
// writes the first five primary slots; it then either completes the primary bank with four more or
// seeds the five secondary slots instead, which is what the gamestate-7 levels do.
constexpr std::uint32_t kPrimaryBank[9] = {
    kDrawBufferA,
    kOverlayInitHook,
    kGamestate0EarlyHook,
    kOverlayParticleSpawn,
    kGamestate0LateHook,
    kPrimaryTailHook0,
    kPrimaryTailHook1,
    kPrimaryTailHook2,
    kTransitionDrawHook,
};
constexpr std::uint32_t kSecondaryBank[5] = {
    kFallImpactCallback,
    kSecondaryHook0,
    kGamestate7Handler,
    kGamestate7DrawHook,
    kSecondaryHook1,
};

struct LevelHandlerSet {
  std::uint8_t primaryCount;   // 0, 5 or 9 primary slots this level installs
  std::uint8_t secondaryCount; // 0 or 5: the gamestate-7 levels also seed the secondary bank
  std::uint32_t primary[9];
  std::uint32_t secondary[5];
};

constexpr std::size_t kLevelCount = 0x64u;
constexpr std::size_t kHandlerSetCount = 44u;

// Level id to handler set: the retail jump table at 0x800113A4, one word per level id, read in
// order. Set 43 is the epilogue — the level ids the table points at it carry no hooks of their own.
constexpr std::uint8_t kLevelHandlerSet[kLevelCount] = {
    0,  1,  2,  3,  4,  43, 43, 5,  43, 6,  7,  8,  9,  10, 11, 12, 43, 43, 43, 43,
    13, 14, 15, 16, 17, 18, 43, 43, 43, 43, 19, 20, 21, 22, 23, 24, 43, 43, 43, 43,
    25, 26, 27, 28, 29, 30, 43, 43, 43, 43, 31, 32, 33, 34, 35, 36, 43, 43, 43, 43,
    37, 38, 39, 40, 41, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
    43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 42,
};

// One row per case body, in address order, and the epilogue last. Each row's comment is the guest
// case it was decoded from, so a reviewer can read the row against the retail code beside it.
constexpr LevelHandlerSet kHandlerSets[kHandlerSetCount] = {
    // 8005A4A0
    {9,
     0,
     {0x80082068u,
      0x80080548u,
      0x8007D8E0u,
      0x80081568u,
      0x80080A0Cu,
      0x8007ADB8u,
      0x8007AEF4u,
      0x8007AF38u,
      0x8007CED8u}},
    // 8005A538
    {5, 0, {0x8007BFF0u, 0x8007B070u, 0x8007AC8Cu, 0x8007B8ACu, 0x8007B144u}},
    // 8005A588
    {5, 0, {0x8007F490u, 0x8007D970u, 0x8007AD08u, 0x8007E990u, 0x8007DE34u}},
    // 8005A5D8
    {5, 0, {0x8007C7B0u, 0x8007B0ACu, 0x8007ACC8u, 0x8007BC50u, 0x8007B180u}},
    // 8005A628
    {5, 0, {0x8007C204u, 0x8007B08Cu, 0x8007ACA8u, 0x8007B9F0u, 0x8007B160u}},
    // 8005A678
    {5, 0, {0x8007F2F4u, 0x8007D970u, 0x8007AD08u, 0x8007E8E4u, 0x8007DE34u}},
    // 8005A6C8
    {5, 0, {0x8007C204u, 0x8007B08Cu, 0x8007ACA8u, 0x8007B9F0u, 0x8007B160u}},
    // 8005A718
    {9,
     0,
     {0x80088620u,
      0x800857CCu,
      0x8007D9C8u,
      0x800873E0u,
      0x80086134u,
      0x8007AEA0u,
      0x8007AFDCu,
      0x8007B020u,
      0x8007CFC0u}},
    // 8005A7B0
    {9,
     0,
     {0x8008A3B8u,
      0x8008772Cu,
      0x8007DA78u,
      0x800892C4u,
      0x80088098u,
      0x8007AF50u,
      0x8007B08Cu,
      0x8007B0D0u,
      0x8007D070u}},
    // 8005A848
    {5, 0, {0x80085594u, 0x80082960u, 0x8007AE40u, 0x800844A0u, 0x80083274u}},
    // 8005A898
    {9,
     0,
     {0x80089ECCu,
      0x800872A4u,
      0x8007DA54u,
      0x80088F68u,
      0x80087E20u,
      0x8007AF2Cu,
      0x8007B068u,
      0x8007B0ACu,
      0x8007D04Cu}},
    // 8005A930
    {5, 0, {0x80084A10u, 0x80081DA8u, 0x8007AF94u, 0x8008391Cu, 0x800826F0u}},
    // 8005A980
    {5,
     5,
     {0x80084ED0u, 0x8008249Cu, 0x8007CFB4u, 0x80084128u, 0x80083330u},
     {0x8007AE08u, 0x8007AEDCu, 0x8007B1FCu, 0x8007B68Cu, 0x8007B4B0u}},
    // 8005AA28
    {9,
     0,
     {0x8008CFA4u,
      0x8008A258u,
      0x8007E3A0u,
      0x8008BFF0u,
      0x8008AE28u,
      0x8007B878u,
      0x8007B9B4u,
      0x8007B9F8u,
      0x8007D998u}},
    // 8005AAC0
    {9,
     0,
     {0x8008D600u,
      0x8008A4D0u,
      0x8007E240u,
      0x8008C540u,
      0x8008B1C0u,
      0x8007B718u,
      0x8007B854u,
      0x8007B898u,
      0x8007D838u}},
    // 8005AB58
    {9,
     0,
     {0x8008BAF8u,
      0x80088B88u,
      0x8007E240u,
      0x8008A9A8u,
      0x80089714u,
      0x8007B718u,
      0x8007B854u,
      0x8007B898u,
      0x8007D838u}},
    // 8005ABF0
    {5, 0, {0x80086260u, 0x80083608u, 0x8007B4C8u, 0x80085184u, 0x80083F2Cu}},
    // 8005AC40
    {5, 0, {0x80087210u, 0x800845F0u, 0x8007AEB8u, 0x8008611Cu, 0x80084EF0u}},
    // 8005AC90
    {5,
     5,
     {0x80083BF0u, 0x800819BCu, 0x8007CFB4u, 0x80082F58u, 0x80082300u},
     {0x8007AE08u, 0x8007AEDCu, 0x8007B1FCu, 0x8007B68Cu, 0x8007B4B0u}},
    // 8005AD38
    {9,
     0,
     {0x8008E608u,
      0x8008B2C0u,
      0x8007E398u,
      0x8008D2D0u,
      0x8008BE98u,
      0x8007B870u,
      0x8007B9ACu,
      0x8007B9F0u,
      0x8007D990u}},
    // 8005ADD0
    {5, 0, {0x8008DEC0u, 0x8008A36Cu, 0x8007BB00u, 0x8008C9D8u, 0x8008AF54u}},
    // 8005AE20
    {5, 0, {0x8008C73Cu, 0x8008883Cu, 0x8007B64Cu, 0x8008B0B0u, 0x80089454u}},
    // 8005AE70
    {5, 0, {0x8008A8A0u, 0x80086DD8u, 0x8007B7A8u, 0x80089450u, 0x80087B40u}},
    // 8005AEC0
    {5, 0, {0x8008749Cu, 0x80083AB4u, 0x8007AF28u, 0x80085F40u, 0x80084830u}},
    // 8005AF10
    {5,
     5,
     {0x80084390u, 0x80081F0Cu, 0x8007CFB4u, 0x800836F8u, 0x80082AA0u},
     {0x8007AE08u, 0x8007AEDCu, 0x8007B1FCu, 0x8007B68Cu, 0x8007B4B0u}},
    // 8005AFB8
    {9,
     0,
     {0x8008AB70u,
      0x80087EF0u,
      0x8007E18Cu,
      0x80089AB8u,
      0x800888F8u,
      0x8007B664u,
      0x8007B7A0u,
      0x8007B7E4u,
      0x8007D784u}},
    // 8005B050
    {5, 0, {0x80087944u, 0x8008465Cu, 0x8007B5DCu, 0x800866D8u, 0x800853ACu}},
    // 8005B0A0
    {5, 0, {0x80087130u, 0x80084718u, 0x8007AFBCu, 0x800861CCu, 0x80085084u}},
    // 8005B0F0
    {5, 0, {0x80089848u, 0x80086B38u, 0x8007B698u, 0x8008869Cu, 0x80087400u}},
    // 8005B140
    {5, 0, {0x8008A69Cu, 0x800874FCu, 0x8007B770u, 0x800894B0u, 0x80088178u}},
    // 8005B190
    {5,
     5,
     {0x80084844u, 0x8008223Cu, 0x8007CFB4u, 0x80083BACu, 0x80082F54u},
     {0x8007AE08u, 0x8007AEDCu, 0x8007B1FCu, 0x8007B68Cu, 0x8007B4B0u}},
    // 8005B238
    {9,
     0,
     {0x8008BB38u,
      0x80088E24u,
      0x8007E3C0u,
      0x8008AA24u,
      0x800897FCu,
      0x8007B898u,
      0x8007B9D4u,
      0x8007BA18u,
      0x8007D9B8u}},
    // 8005B2D0
    {5, 0, {0x800880D4u, 0x80084B94u, 0x8007B4F8u, 0x80086D38u, 0x800857FCu}},
    // 8005B320
    {5, 0, {0x8008771Cu, 0x80084620u, 0x8007B4DCu, 0x80086438u, 0x800850A0u}},
    // 8005B370
    {5, 0, {0x80089820u, 0x8008590Cu, 0x8007B510u, 0x800881D8u, 0x80086754u}},
    // 8005B3C0
    {5, 0, {0x80086348u, 0x800836A8u, 0x8007AF50u, 0x80085254u, 0x80084028u}},
    // 8005B410
    {5,
     5,
     {0x80084934u, 0x80082028u, 0x8007CFB4u, 0x80083B8Cu, 0x80082D94u},
     {0x8007AE08u, 0x8007AEDCu, 0x8007B1FCu, 0x8007B68Cu, 0x8007B4B0u}},
    // 8005B4B8
    {9,
     0,
     {0x80085CE0u,
      0x80083568u,
      0x8007D938u,
      0x80084EA0u,
      0x80083ED8u,
      0x8007AE10u,
      0x8007AF4Cu,
      0x8007AF90u,
      0x8007CF30u}},
    // 8005B550
    {5, 0, {0x80088668u, 0x80085664u, 0x8007B528u, 0x8008747Cu, 0x80086144u}},
    // 8005B5A0
    {5, 0, {0x80086004u, 0x80083108u, 0x8007AE5Cu, 0x80084EACu, 0x80083B4Cu}},
    // 8005B5F0
    {5, 0, {0x800854B4u, 0x80082F24u, 0x8007AD64u, 0x80084634u, 0x800836F0u}},
    // 8005B640
    {5, 0, {0x80086264u, 0x80083690u, 0x8007AD4Cu, 0x80085230u, 0x800840FCu}},
    // 8005B690
    {5, 0, {0x8007C654u, 0x8007B0C0u, 0x8007ACDCu, 0x8007BC44u, 0x8007B194u}},
    // 8005B6E0
    {0, 0, {}, {}},
};

void applyPerLevelGlobalsTable(Core *c) {
  const std::uint32_t level = c->mem_r32(kActiveLevelId);
  if (level < kLevelCount) {
    const LevelHandlerSet &set = kHandlerSets[kLevelHandlerSet[level]];
    for (std::size_t index = 0; index < set.primaryCount; ++index) {
      c->mem_w32(kPrimaryBank[index], set.primary[index]);
    }
    for (std::size_t index = 0; index < set.secondaryCount; ++index) {
      c->mem_w32(kSecondaryBank[index], set.secondary[index]);
    }
  }
  c->r[3] = level;
  c->r[1] = 0x80080000u; // the epilogue's lui, the value $at holds on return
  c->r[2] = c->mem_r32(kDrawBufferA);
  c->mem_w32(kDrawBufferB, c->r[2]);
}

} // namespace

void registerLevelGlobalsTableOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8005A470u, "apply_per_level_globals_table", applyPerLevelGlobalsTable);
}

} // namespace spyro1::native
