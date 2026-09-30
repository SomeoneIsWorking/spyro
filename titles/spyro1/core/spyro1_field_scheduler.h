#pragma once

#include "field_owner.h"
#include "guest_globals.h"
#include "spyro1_frame_policy.h"

#include <cstdint>
#include <string_view>

class Core;
class Game;

namespace spyro1 {

// Spyro 1's own field vocabulary is the shared one; the alias keeps every existing call site
// and test reading as Spyro 1's contract rather than a framework one.
using FieldRequest = spyro::FieldRequest;

struct SkipMapSample;

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
  // tick so "the card was never published" cannot be read as "the card is at tick 0".
  bool cardLayoutResident = false;
  std::uint32_t cardTick = 0;
  // titlescreen.c:105's own gate, and nothing else: a press cannot skip the card below this tick.
  bool cardSkippable = false;
};

// Retail's own card gate, read from SCUS_942.28 via external/spyro-1/src/overlays/titlescreen.c:
// 100-106. Exposed so a test and the log cannot disagree about it.
inline constexpr std::uint32_t kCardSkipTick = 300u;

SkipMapSample readSkipMapSample(Core &core);

// Spyro 1's field owner: the shared delivery sequence plus the two things that are this title's
// alone -- the boot-sequence window in which a Start edge is observed, and the per-field skip
// map over the title screen. Guest libetc VSync at 0x8005DBC4 is a mandatory fatal trap: the
// native frame tail replaced every call to it, so it is never reached from guest code.
class FieldScheduler final : public spyro::FieldObserver {
public:
  explicit FieldScheduler(Game &game);

  // Publish this scheduler as the per-Core field owner. Explicit, like the shared owner's: a
  // constructor that reaches the shared context makes every fixture's member ORDER a hidden
  // precondition, and `tests/test_field_scheduler.cpp` builds its scheduler before it publishes its
  // context.
  void publish();

  bool deliver(const spyro::FieldRequest &request);

  void beginLogicFrame() {
    fields_.beginLogicFrame();
  }

  bool finishLogicFrame() const {
    return fields_.finishLogicFrame();
  }

  std::uint32_t fieldsThisLogicFrame() const {
    return fields_.fieldsThisLogicFrame();
  }

  void bootSequenceBegin();
  void bootSequenceEnd();
  void armHostClock();
  void observeVblankCallback(std::uint32_t function);

  // BootSequence alone decides whether this edge transitions a presentation-only hold. The pad
  // subsystem continues to expose the input to later title states unchanged.
  bool presentationSkipPressed() const;

  std::int32_t counter() const;
  std::string_view activeDeliverySite() const;

  void onField(Core &core, bool startEdge) override;

private:
  spyro::FieldOwner fields_;
  bool bootSequenceActive_ = false;
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

} // namespace spyro1
