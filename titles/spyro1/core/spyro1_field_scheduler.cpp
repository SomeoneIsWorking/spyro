#include "spyro1_field_scheduler.h"
#include "guest_globals.h"

#include "cfg.h"
#include "core.h"
#include "frame_pacer.h"
#include "game.h"
#include "guest_call.h"
#include "hle.h"
#include "host_turn.h"
#include "repl.h"
#include "runtime_run.h"
#include "snapshot.h"
#include "spyro1_frame_driver.h"
#include "spyro1_vblank_irq.h"
#include "spyro_game.h"

#include <chrono>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro1 {
namespace {

constexpr std::uint16_t kPadStart = 0x0008u;
constexpr std::uint16_t kPadCross = 0x4000u;
constexpr std::uint32_t kVblankCounter = 0x800749E0u;
constexpr std::uint32_t kRootHandlers = 0x80073928u;
constexpr std::uint32_t kHandlerStackTop = 0x8000E000u;
constexpr std::uint32_t kHandlerStackBytes = 8192u;
constexpr std::uint32_t kHandlerStackFloor = kHandlerStackTop - kHandlerStackBytes;
constexpr std::uint32_t kStackPoison = 0xCDCDCDCDu;

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

double monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  static const auto start = Clock::now();
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::uint32_t handlerStackLowWater(Core &core) {
  for (std::uint32_t address = kHandlerStackFloor; address < kHandlerStackTop; address += 4) {
    if (core.mem_r32(address) != kStackPoison) {
      return address;
    }
  }
  return kHandlerStackTop;
}

} // namespace

FieldScheduler::FieldScheduler(Game &game) : game_(game) {}

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

void FieldScheduler::beginLogicFrame() {
  cadence_.beginLogicFrame();
}

bool FieldScheduler::finishLogicFrame() const {
  return cadence_.completesLogicFrame();
}

std::uint32_t FieldScheduler::fieldsThisLogicFrame() const {
  return cadence_.fields();
}

std::int32_t FieldScheduler::counter() const {
  return static_cast<std::int32_t>(game_.core.mem_r32(kVblankCounter));
}

std::string_view FieldScheduler::activeDeliverySite() const {
  return inField_ && activeDeliverySite_ != nullptr ? std::string_view(activeDeliverySite_)
                                                    : std::string_view{};
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

void FieldScheduler::armHostClock() {
  if (hostClockArmed_) {
    lucent::error("fields", "Spyro 1 host field clock armed twice");
    std::abort();
  }
  hostClockArmed_ = true;
  psx::cpu::registerHostTurn(game_.core, hostTurn, gpu_field_rate_millihz(&game_.core));
  lucent::info("fields", "native host field clock armed at the gameplay boundary");
}

void FieldScheduler::observeVblankCallback(std::uint32_t function) {
  if (function == callbackFallback_) {
    return;
  }
  callbackFallback_ = function;
  lucent::info(
      "fields", "VSyncCallback(0x{:08X}) registered for host-owned field delivery", function);
}

bool FieldScheduler::dispatchCallbacks() {
  Core &core = game_.core;
  const std::uint32_t root = core.mem_r32(kRootHandlers);
  const std::uint32_t target = root != 0 ? root : callbackFallback_;
  if (target == 0) {
    return false;
  }
  const bool rootOwnsIrq = root != 0 && game_.hle.exception_exit_buf != 0;
  if (rootOwnsIrq && (!hasPendingEnabledVblank(core.irqStatLatch(), game_.hle.i_mask) ||
                      !game_.hle.canDispatchInterrupt(core))) {
    // An installed guest IRQ continuation retains counter ownership while its edge is masked or
    // its CPU context is unavailable. Calling the root directly here would duplicate that edge
    // when the runtime later services it. Leave both hardware state and pending work untouched.
    return true;
  }
  if (!handlerStackArmed_) {
    handlerStackArmed_ = true;
    for (std::uint32_t address = kHandlerStackFloor; address < kHandlerStackTop; address += 4) {
      core.mem_w32(address, kStackPoison);
    }
    lucent::info("fields",
                 "guest vblank callbacks use IRQ stack [0x{:08X},0x{:08X})",
                 kHandlerStackFloor,
                 kHandlerStackTop);
  }

  const std::int32_t before = counter();
  R3000 saved = static_cast<R3000 &>(core);
  core.r[29] = kHandlerStackTop;
  if (rootOwnsIrq) {
    // A real display edge owns this dispatch. The guest's HookEntryInt context is a saved
    // continuation, not the root handler itself, so direct rc0(root) would run both the resumed
    // IRQ path and the root once guest irq poll reaches the latched edge. Re-arm the gate from the
    // same I_STAT fact it represents, then let Hle restore and unwind that continuation exactly
    // once. Other IRQ sources remain Hle's responsibility; they never select this branch alone.
    core.pending_work |= Core::PW_IRQ;
    game_.hle.irqPoll(&core);
  } else {
    // Without a guest IRQ continuation, the title's measured direct callback route owns delivery.
    psx::cpu::dispatchGuestToReturn0(
        core, target, psx::cpu::ExecutionBudget::currentTurn(core), "field-callback");
  }
  static_cast<R3000 &>(core) = saved;

  if (core.mem_r32(kHandlerStackFloor) != kStackPoison) {
    lucent::error("fields",
                  "guest vblank callback overflowed its {}-byte IRQ stack at 0x{:08X}",
                  kHandlerStackBytes,
                  kHandlerStackFloor);
    std::abort();
  }
  static const lucent::Channel channel{"fields"};
  if (channel) {
    const std::uint32_t lowWater = handlerStackLowWater(core);
    if (lowWater < deepestHandlerStack_) {
      deepestHandlerStack_ = lowWater;
      lucent::debug(channel, "vblank callback stack peak: {} bytes", kHandlerStackTop - lowWater);
    }
  }

  // A root actually dispatched here must tick exactly once. Masked/critical guest IRQ ownership
  // returned above: physical fields continue, but their counter tick waits for IRQ delivery.
  const std::int32_t after = counter();
  if (root != 0 && after != before + 1) {
    lucent::error("fields",
                  "guest vblank root advanced counter {} -> {}; host scheduler requires exactly "
                  "one field",
                  before,
                  after);
    std::abort();
  }
  return root != 0;
}

void FieldScheduler::serviceSkipMap(bool startEdge) {
  Core &core = game_.core;
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
  if (skipMapFields_ % 600u == 0) {
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

void FieldScheduler::serviceRepl() {
  Core &core = game_.core;
  if (!cfg_on("PSXPORT_REPL")) {
    return;
  }
  if (!replQuit_ && replBudget_ <= 0) {
    while ((replBudget_ = game_.repl.read(&core, core.mem_r32(kVblankCounter))) == 0) {
    }
    if (replBudget_ == -2) {
      lucent::info("repl", "end — ending the run cleanly");
      spyro::runtimeRun(core).requestEnd();
      replQuit_ = true;
    }
    if (replBudget_ < 0) {
      replQuit_ = true;
      lucent::info("repl", "quit — running free");
    }
  }
  if (replBudget_ > 0) {
    --replBudget_;
  }
}

void FieldScheduler::reportField(const FieldRequest &request,
                                 int queueSize,
                                 bool queueWasUnconsumed) {
  lucent::debug("pace",
                "t={:.1f}ms vbl={} pace={} present={} rq_unconsumed={} | site={} quota={} "
                "counter={} rq_n={} unconsumed={}",
                monotonicMilliseconds(),
                fields_,
                paces_,
                presents_,
                queueFirstConsumers_,
                request.site,
                game_.core.cfg ? game_.core.cfg->paceQuota : 0u,
                counter(),
                queueSize,
                queueWasUnconsumed ? 1 : 0);
}

bool FieldScheduler::deliver(const FieldRequest &request) {
  Core &core = game_.core;
  if (inField_) {
    ++refused_;
    lucent::debug("pace",
                  "field refused at {}: one is already in flight (refused={})",
                  request.site,
                  refused_);
    return false;
  }
  inField_ = true;
  activeDeliverySite_ = request.site;

  // The REPL parks BEFORE this field's host pad sample and guest VBlank handler, where the
  // full-console reference parks too (its VBlank step stops with the handler pending). A hold
  // committed at the park is then seen by this field's SIO exchange on both cores, and RAM read
  // at the park is the same guest phase on both: after the main loop's last iteration, before
  // the next handler. Parking after the handler cost the product one field of pad latency
  // against the reference and put every pad-word sample one poll ahead of it.
  serviceRepl();

  const int queueSize = game_.rq.n;
  const bool queueWasUnconsumed = queueSize > 0 && !game_.rq.consumed;
  queueFirstConsumers_ += queueWasUnconsumed ? 1u : 0u;
  if (request.present) {
    game_.rq.flush(&core);
  }

  game_.pad.serviceFrame();
  const bool startDown = (game_.pad.buttons & kPadStart) == 0;
  const bool startEdge = startDown && (previousButtons_ & kPadStart) != 0;
  previousButtons_ = game_.pad.buttons;
  if (!game_.timing.advanceDisplayFields(1, 1, gpu_field_rate_millihz(&core))) {
    lucent::error("fields", "display clock refused one field at {}", request.site);
    std::abort();
  }
  const bool guestRootOwnsCounter = dispatchCallbacks();
  if (!guestRootOwnsCounter) {
    core.mem_w32(kVblankCounter, core.mem_r32(kVblankCounter) + 1u);
  }
  cadence_.delivered();
  serviceSkipMap(startEdge);
  snapshot_tick(&core);

  if (request.present) {
    // Every visible field crosses the framework's one presentation fence. This is the same owner
    // the native gameplay path reaches through Fps60::frame_commit; boot/upload fields simply have
    // no temporal decorator. A raw gpu_present here showed pixels but left FrameLoopShell's product
    // boundary at fence zero, so the host could not prove one-and-only-one presentation per step.
    game_.presentation.commit(&core, request.pace ? 1 : 0);
    ++presents_;
  }
  game_.spu_audio.frame();
  if (request.pace) {
    ++paces_;
  }
  const std::array<std::uint32_t, 2> eventClasses =
      core.cfg != nullptr ? std::array{core.cfg->irqEventClasses[0], core.cfg->irqEventClasses[1]}
                          : std::array{0xF0000009u, 0xF2000003u};
  for (std::uint32_t eventClass : eventClasses) {
    if (eventClass != 0) {
      game_.hle.deliverEvent(eventClass, 0xFFFFFFFFu);
    }
  }

  ++fields_;
  reportField(request, queueSize, queueWasUnconsumed);
  activeDeliverySite_ = nullptr;
  inField_ = false;
  spyro::runtimeRun(core).fieldDelivered();
  return true;
}

bool FieldScheduler::presentationSkipPressed() const {
  return game_.pad.pressedButton(kPadStart | kPadCross);
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

void hostTurn(Core *core) {
  FieldScheduler &scheduler = fieldScheduler(*core);
  // A host turn may deliver the next guest field while a finite update is still executing, but it
  // is not a second display owner. The enclosing FrameDriver step reaches exactly one presentation
  // fence at its native frame commit; presenting here made that same step advance the fence twice.
  scheduler.deliver({.site = "hostturn", .present = false, .pace = false});
}

} // namespace spyro1
