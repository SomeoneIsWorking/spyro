#pragma once

#include <cstdint>
#include <vector>

class Core;

// GS_Dragon (stage 8) is drawn by 0x8001CFDC, which is not one producer but eight authored
// compositions selected by g_DragonCutscene.m_State. Most of them replace retail's usual moby setup
// with an EXPLICIT pointer list they write themselves — the rescued dragon, the cutscene Spyro and
// dragon, the crystal fragments still on screen, the HUD counters — so the composition and the list
// construction are one decision and belong in one place. This module derives both without touching
// the queue; the owning producer applies them.
namespace spyro::dragon_scene {

// The guest state 0x8001CFDC branches on.
struct State {
  uint32_t state = 0;
  int32_t ticks = 0;
  int32_t fade = 0;
  uint32_t cutsceneSpyro = 0;
  uint32_t cutsceneDragon = 0;
  uint32_t rescuedDragon = 0;
  bool borderEnabled = false;
  // D_80076248.unk_0x0 gates the burst effect 0x80058864 that runs before every state's branch.
  bool burstActive = false;
};

State read(Core *core);

// One authored call, in the order 0x8001CFDC makes it. The three that write no primitives are still
// steps rather than preconditions, because their position in the sequence decides what the drawing
// producers afterwards can see.
enum class Producer : uint8_t {
  QueueMobys,    // 0x800521C0, the level-array classification
  RescuedText,   // 0x80018728, builds the "Rescued <name>" HUD text mobys
  CopyHudMobys,  // 0x80018880, appends the HUD moby range to the shaded list
  CameraShake,   // state 5's own scaling of the draw-state shake vectors
  ClearDrawList, // the Memset of m_Moby retail performs once the list has been consumed
  Regular,       // 0x8001F158 + 0x8001F798
  Secondary,     // 0x800208FC + 0x80020F34
  Shaded,        // 0x80022A2C
  MobyShadows,   // 0x80059F8C
  SpyroModel,    // 0x80023AC4
  SpyroShadow,   // 0x80059A48
  Environment,   // 0x8002B9CC
  Cyclorama,     // 0x80050BD0
  Particles,     // 0x800573C8
  ScreenFade,    // 0x800190D4
  ScreenBorder,  // 0x80018F30
  FieldChain,    // 0x80019698, the whole model chain state 0 shares with FIELD
};

// Which explicit pointer lists this state writes before drawing. An empty list that is still
// `write` is a list retail terminates at its first slot, which is not the same as leaving the
// previous state's pointers in place.
struct Lists {
  bool writeDraw = false;
  bool writeShaded = false;
  std::vector<uint32_t> draw;
  std::vector<uint32_t> shaded;
};

enum class Status : uint8_t {
  Ready,
  InvalidCore,
  UnterminatedMobyArray,
  ListCapacityExceeded,
  UnknownState,
};

struct Plan {
  Status status = Status::InvalidCore;
  std::vector<Producer> producers;
  Lists lists;
  // States that build their own draw list feed 0x8001F798 from it instead of the level array.
  bool explicitDrawSource = false;
};

Plan plan(Core *core, const State &state);

// Writes the state's pointer lists and the m_Moby clear 0x8001CFDC performs after preparing. Split
// from plan so the derivation stays pure and the guest write happens at one named step.
void commit(Core *core, const Plan &plan);

// The Memset 0x8001CFDC performs once the draw list has been consumed.
void clearDrawList(Core *core);

constexpr uint32_t kDrawList = 0x8006FCF4u;   // g_SonyImage.u.m_Draw.m_Moby
constexpr uint32_t kShadedList = 0x800720F4u; // g_SonyImage.m_ShadedMobys
constexpr uint32_t kDrawListBytes = 0x900u;   // the m_Moby array 0x8001CFDC clears
constexpr uint32_t kFragmentClass = 251u;     // MOBYCLASS_CRYSTAL_DRAGON_FRAGMENT
constexpr uint32_t kHudCounterMoby = 5u;      // g_Hud.m_Mobys + 5: the gem/dragon/life counters
constexpr uint32_t kHudCounters = 3u;
// g_SonyImage.m_ShadedMobys holds 256 pointers and must keep room for its terminator.
constexpr uint32_t kShadedCapacity = 255u;
const char *producerName(Producer producer);
const char *statusName(Status status);

} // namespace spyro::dragon_scene
