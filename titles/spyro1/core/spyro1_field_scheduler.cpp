#include "spyro1_field_scheduler.h"
#include "guest_globals.h"

#include "core.h"
#include "game.h"
#include "runtime_run.h"
#include "spyro1_frame_driver.h"
#include "spyro_context.h"
#include "spyro_game.h"

#include <lucent/log.h>
#include <stdlib.h>

namespace spyro1 {
namespace {

constexpr std::uint32_t kVblankCounter = 0x800749E0u;
constexpr std::uint32_t kRootHandlers = 0x80073928u;
constexpr std::uint32_t kHandlerStackTop = 0x8000E000u;
constexpr std::uint32_t kHandlerStackBytes = 8192u;

// g_TitlescreenState's own offsets (external/spyro-1/include/titlescreen.h, four-byte fields in
// declaration order). m_Mode is at the struct BASE, which is what `mem_r32(kTitlescreenState)`
// returns; m_State is +0x04; m_SubState is +0x10.
constexpr std::uint32_t kTitleState = spyro::guest::kTitlescreenState + 0x04u;
constexpr std::uint32_t kTitleSubState = spyro::guest::kTitlescreenState + 0x10u;

// g_CutsceneLayout (loaders.c:955, the one writer of this word in the whole image), and
// CutsceneLayout.m_CurrentTick, its first int (external/spyro-1/include/cutscene.h:20-27).
//
// RECOVERED FROM SCUS_942.28, not taken on trust. tools/probe_title_card.py --static prints the
// words: 0x80075680 has exactly ONE `lui $rX,0x8007` + `sw 0x5680($rX)` writer in all 103,936
// instruction words of the main image, at 0x80014A38, and SEVENTEEN `lui`+`lw` reads of it as a
// pointer base, three of them within 0x200 bytes of the writer (0x80014A84, 0x80014AE8, 0x80014B0C)
// -- the PATCH_POINTER of m_CameraData and the Moby-pointer loop that read it straight back. The
// same report establishes the module map those words live in: main image 0x80010000..0x80075800,
// guest bss 0x80075640..0x8007AA38, module arena 0x8007AA38, and the title overlay's own update
// entry at arena+0x174 = 0x8007ABAC, which is the `jal` in the gamestate-13 arm.
//
// A null layout reads as "not resident" rather than being dereferenced, so a card whose cutscene
// was never published reports itself instead of taking the port down.
constexpr std::uint32_t kCutsceneLayout = 0x80075680u;
constexpr std::uint32_t kCutsceneCurrentTick = 0u;
constexpr std::uint32_t kNoCardLayout = 0xFFFFFFFFu;

} // namespace

FieldScheduler::FieldScheduler(Game &game)
    : fields_(game,
              spyro::FieldOwnerFacts{
                  .titleName = "Spyro 1",
                  .fieldCounter = kVblankCounter,
                  .rootHandlerSlot = kRootHandlers,
                  .handlerStackTop = kHandlerStackTop,
                  .handlerStackBytes = kHandlerStackBytes,
                  .fieldsPerLogicFrame = spyro1::kFieldsPerLogicFrame,
              },
              this) {}

SkipMapSample readSkipMapSample(Core &core) {
  SkipMapSample sample{.gamestate = core.mem_r32(spyro::guest::kGamestate),
                       .loadStage = core.mem_r32(spyro::guest::kLoadStage),
                       .title = {.mode = core.mem_r32(spyro::guest::kTitlescreenState),
                                 .state = core.mem_r32(kTitleState),
                                 .subState = core.mem_r32(kTitleSubState)}};
  const std::uint32_t layout = core.mem_r32(kCutsceneLayout);
  sample.cardLayoutResident = layout != 0u;
  if (sample.cardLayoutResident) {
    sample.cardTick = core.mem_r32(layout + kCutsceneCurrentTick);
    sample.cardSkippable = sample.cardTick >= kCardSkipTick;
  }
  return sample;
}

void FieldScheduler::bootSequenceBegin() {
  if (bootSequenceActive_) {
    lucent::error("skipmap", "boot sequence observation armed twice");
    std::abort();
  }
  bootSequenceActive_ = true;
  lucent::debug("skipmap", "observing Start edges during guest boot function 0x800127C0");
}

void FieldScheduler::bootSequenceEnd() {
  bootSequenceActive_ = false;
}

void FieldScheduler::publish() {
  fields_.publish();
}

void FieldScheduler::armHostClock() {
  fields_.publish();
  fields_.armHostClock();
}

void FieldScheduler::observeVblankCallback(std::uint32_t function) {
  fields_.observeVblankCallback(function);
}

std::int32_t FieldScheduler::counter() const {
  return fields_.counter();
}

std::string_view FieldScheduler::activeDeliverySite() const {
  return fields_.activeDeliverySite();
}

bool FieldScheduler::presentationSkipPressed() const {
  return fields_.presentationSkipPressed();
}

bool FieldScheduler::deliver(const spyro::FieldRequest &request) {
  return fields_.deliver(request);
}

void FieldScheduler::onField(Core &core, bool startEdge) {
  const SkipMapSample sample = readSkipMapSample(core);

  ++skipMapFields_;
  const bool bootActive = bootSequenceActive_;
  bootActive ? ++skipMapBootFields_ : ++skipMapStageFields_;
  if (startEdge) {
    ++skipMapStartEdges_;
  }
  const bool changed =
      sample.gamestate != previousGamestate_ || sample.loadStage != previousLoadStage_ ||
      sample.title.mode != previousTitleMode_ || sample.title.state != previousTitleState_ ||
      sample.title.subState != previousTitleSubState_ || bootActive != previousBootActive_;
  if (startEdge || changed) {
    lucent::debug("skipmap",
                  "field={} start_edge={} region={} load_stage={} gamestate={} "
                  "title[mode={} state={} substate={}] card_tick={} card_skippable={} edges={}",
                  skipMapFields_,
                  startEdge ? 1 : 0,
                  bootActive ? "boot" : "stage",
                  sample.loadStage,
                  sample.gamestate,
                  sample.title.mode,
                  sample.title.state,
                  sample.title.subState,
                  sample.cardLayoutResident ? sample.cardTick : kNoCardLayout,
                  sample.cardSkippable ? 1 : 0,
                  skipMapStartEdges_);
  }
  if (skipMapFields_ % 600u == 0u) {
    lucent::debug("skipmap",
                  "scanned {} fields: start_edges={} boot_fields={} stage_fields={} current={}",
                  skipMapFields_,
                  skipMapStartEdges_,
                  skipMapBootFields_,
                  skipMapStageFields_,
                  bootActive ? "boot" : "stage");
  }
  previousGamestate_ = sample.gamestate;
  previousTitleMode_ = sample.title.mode;
  previousTitleState_ = sample.title.state;
  previousTitleSubState_ = sample.title.subState;
  previousLoadStage_ = sample.loadStage;
  previousBootActive_ = bootActive;
}

FieldScheduler &fieldScheduler(Core &core) {
  return frameDriver(core).fields();
}

const FieldScheduler &fieldScheduler(const Core &core) {
  return frameDriver(core).fields();
}

bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending) {
  return fieldScheduler(core).deliver(
      {.site = site, .present = !fps60CommitPending, .pace = !fps60CommitPending});
}

void beginBootSequence(Core &core) {
  fieldScheduler(core).bootSequenceBegin();
}

void endBootSequence(Core &core) {
  fieldScheduler(core).bootSequenceEnd();
}

void observeVblankCallback(Core &core, std::uint32_t function) {
  fieldScheduler(core).observeVblankCallback(function);
}

} // namespace spyro1
