#pragma once

#include "guest_globals.h"
#include "spyro1_frame_policy.h"

#include <cstdint>
#include <string_view>

class Core;
class Game;

namespace spyro1 {

struct FieldRequest {
  const char *site;
  bool present;
  bool pace;
};

// The four words of g_TitlescreenState the skip map reads, named as the struct declares them.
//
// WHY THE NAMES EXIST. `core.mem_r32(kTitlescreenState)` returns the value at the struct BASE,
// which is m_Mode -- it is not a substate of anything. The offset the map used to read as its
// second field, 0x80078D7C, is m_State, the save picker's stage. m_SubState, the field that
// actually says which title screen is up, is at +0x10 and NOTHING read it, which is why the card
// could not appear in this map's telemetry at all.
struct TitleStateSample {
  std::uint32_t mode = 0;     // +0x00, TSM_Init / TSM_Menu / TSM_Loading / TSM_Demo
  std::uint32_t state = 0;    // +0x04, the save picker's stage
  std::uint32_t subState = 0; // +0x10, which title screen of the mode is up
};

// One field's reading of everything the skip map reports, so the log and a test read the SAME guest
// words through the SAME code. Before this was a struct the map read the words inline in a log
// call, which left the offsets unasserted and two of the three misnamed.
struct SkipMapSample {
  std::uint32_t gamestate = 0;
  std::uint32_t loadStage = 0;
  TitleStateSample title;
  // g_CutsceneLayout, and the card's own clock through it. `layoutResident` is separate from the
  // tick so "the cutscene layout was never published" cannot be read as "the card is at tick 0".
  bool cardLayoutResident = false;
  std::uint32_t cardTick = 0;
  // titlescreen.c:105's own gate, and nothing else: a press cannot skip the card below this tick.
  bool cardSkippable = false;
};

// The retail card's Start gate, read from SCUS_942.28 via external/spyro-1/src/overlays/
// titlescreen.c:100-106. Exposed so a test and the log cannot disagree about it.
inline constexpr std::uint32_t kCardSkipTick = 300u;

SkipMapSample readSkipMapSample(Core &core);

// The sole Spyro 1 definition of one 60 Hz display field. Native boot, frame tails, and host turns
// call it directly; guest VSync is a mandatory fatal trap and never reaches this owner.
class FieldScheduler {
public:
  explicit FieldScheduler(Game &game);

  bool deliver(const FieldRequest &request);
  void beginLogicFrame();
  bool finishLogicFrame() const;
  std::uint32_t fieldsThisLogicFrame() const;

  void bootSequenceBegin();
  void bootSequenceEnd();
  void armHostClock();
  void observeVblankCallback(std::uint32_t function);

  // BootSequence alone decides whether this edge transitions a presentation-only hold. The pad
  // subsystem continues to expose the input to later title states unchanged.
  bool presentationSkipPressed() const;

  std::int32_t counter() const;
  std::string_view activeDeliverySite() const;

private:
  // True while a guest root owns its counter, including an IRQ deferred by masking/critical state.
  bool dispatchCallbacks();
  void serviceRepl();
  void serviceSkipMap(bool startEdge);
  void reportField(const FieldRequest &request, int queueSize, bool queueWasUnconsumed);

  Game &game_;
  bool bootSequenceActive_ = false;
  FieldCadence cadence_{};
  bool inField_ = false;
  const char *activeDeliverySite_ = nullptr;
  bool handlerStackArmed_ = false;
  bool hostClockArmed_ = false;
  bool replQuit_ = false;
  long replBudget_ = 0;
  std::uint64_t refused_ = 0;
  std::uint64_t fields_ = 0;
  std::uint64_t paces_ = 0;
  std::uint64_t presents_ = 0;
  std::uint64_t queueFirstConsumers_ = 0;
  std::uint32_t callbackFallback_ = 0;
  std::uint32_t deepestHandlerStack_ = 0x8000E000u;
  std::uint16_t previousButtons_ = 0xFFFFu;
  std::uint32_t skipMapFields_ = 0;
  std::uint32_t skipMapBootFields_ = 0;
  std::uint32_t skipMapStageFields_ = 0;
  std::uint32_t skipMapStartEdges_ = 0;
  std::uint32_t previousGamestate_ = ~0u;
  // g_TitlescreenState's own fields, by name: m_Mode is the struct base, m_State is the save
  // picker's stage, and m_SubState is the one that says which title screen is up. The map used to
  // carry two of these under names that named neither, and omitted m_SubState entirely.
  std::uint32_t previousTitleMode_ = ~0u;
  std::uint32_t previousTitleState_ = ~0u;
  std::uint32_t previousTitleSubState_ = ~0u;
  std::uint32_t previousLoadStage_ = ~0u;
  bool previousBootActive_ = false;
};

FieldScheduler &fieldScheduler(Core &core);
const FieldScheduler &fieldScheduler(const Core &core);

bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending);
void beginBootSequence(Core &core);
void endBootSequence(Core &core);
void observeVblankCallback(Core &core, std::uint32_t function);
void hostTurn(Core *core);

} // namespace spyro1
