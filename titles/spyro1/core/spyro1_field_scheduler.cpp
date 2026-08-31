#include "spyro1_field_scheduler.h"

#include "cfg.h"
#include "core.h"
#include "game.h"
#include "guest_call.h"
#include "hle.h"
#include "producer_run.h"
#include "recomp_iface.h"
#include "repl.h"
#include "snapshot.h"
#include "spyro1_frame_driver.h"
#include "spyro1_vblank_irq.h"
#include "spyro_game.h"

#include <cstdlib>
#include <lucent/log.h>
#include <time.h>

namespace spyro1 {
namespace {

constexpr std::uint32_t kBootLogoHoldFields = 0xD2u;
constexpr std::uint16_t kPadStart = 0x0008u;
constexpr std::uint32_t kVblankCounter = 0x800749E0u;
constexpr std::uint32_t kRootHandlers = 0x80073928u;
constexpr std::uint32_t kHandlerStackTop = 0x8000E000u;
constexpr std::uint32_t kHandlerStackBytes = 8192u;
constexpr std::uint32_t kHandlerStackFloor = kHandlerStackTop - kHandlerStackBytes;
constexpr std::uint32_t kStackPoison = 0xCDCDCDCDu;

double monotonicMilliseconds() {
  timespec timestamp{};
  clock_gettime(CLOCK_MONOTONIC, &timestamp);
  static double start = -1.0;
  const double now = timestamp.tv_sec * 1000.0 + timestamp.tv_nsec / 1e6;
  if (start < 0.0) {
    start = now;
  }
  return now - start;
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

void FieldScheduler::bootSkipBegin() {
  boot_skip_begin(bootSkip_);
  lucent::debug("bootskip", "armed for guest boot function 0x800127C0");
}

void FieldScheduler::bootSkipEnd() {
  lucent::debug("bootskip",
                "disarmed: scanned {} boot fields, saw {} fresh edge(s), advanced {} time(s)",
                bootSkip_.fields,
                bootSkip_.edges,
                bootSkip_.advances);
  bootSkip_.active = false;
}

void FieldScheduler::armHostClock() {
  if (hostClockArmed_) {
    lucent::error("fields", "Spyro 1 host field clock armed twice");
    std::abort();
  }
  hostClockArmed_ = true;
  rec_host_turn_register(&game_.core, hostTurn, gpu_field_rate_millihz(&game_.core));
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
  if (shouldDispatchVblankThroughIrq(
          core.irqStatLatch(), game_.hle.i_mask, game_.hle.exception_exit_buf)) {
    // A real display edge owns this dispatch. The guest's HookEntryInt context is a saved
    // continuation, not the root handler itself, so direct rc0(root) would run both the resumed
    // IRQ path and the root once rec_irq_poll reaches the latched edge. Re-arm the gate from the
    // same I_STAT fact it represents, then let Hle restore and unwind that continuation exactly
    // once. Other IRQ sources remain Hle's responsibility; they never select this branch alone.
    core.pending_work |= Core::PW_IRQ;
    rec_irq_poll(&core);
  } else {
    // Host-owned fields have no hardware VBlank edge to dispatch. Keep the measured root fallback
    // for that case only.
    rc0(&core, target);
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

  // The retained root handler still performs the physical counter store. The scheduler owns when
  // that tick occurs and verifies the exact one-field contract; direct native callback-table
  // dispatch remains the next RE boundary.
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
  constexpr std::uint32_t kStage = 0x800757D8u;
  constexpr std::uint32_t kSubstate = 0x80078D78u;
  constexpr std::uint32_t kSubSubstate = 0x80078D7Cu;
  constexpr std::uint32_t kBootPhase = 0x80075864u;
  Core &core = game_.core;

  ++skipMapFields_;
  const bool bootActive = bootSkip_.active;
  bootActive ? ++skipMapBootFields_ : ++skipMapStageFields_;
  if (startEdge) {
    ++skipMapStartEdges_;
  }
  const std::uint32_t stage = core.mem_r32(kStage);
  const std::uint32_t substate = core.mem_r32(kSubstate);
  const std::uint32_t subSubstate = core.mem_r32(kSubSubstate);
  const std::uint32_t bootPhase = core.mem_r32(kBootPhase);
  const bool changed = stage != previousStage_ || substate != previousSubstate_ ||
                       subSubstate != previousSubSubstate_ || bootPhase != previousBootPhase_ ||
                       bootActive != previousBootActive_;
  if (startEdge || changed) {
    lucent::debug("skipmap",
                  "field={} start_edge={} region={} boot_phase={} stage={}/{}/{} edges={}",
                  skipMapFields_,
                  startEdge ? 1 : 0,
                  bootActive ? "boot" : "stage",
                  bootPhase,
                  stage,
                  substate,
                  subSubstate,
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
  previousStage_ = stage;
  previousSubstate_ = substate;
  previousSubSubstate_ = subSubstate;
  previousBootPhase_ = bootPhase;
  previousBootActive_ = bootActive;
}

void FieldScheduler::serviceInspection() {
  Core &core = game_.core;
  snapshot_tick(&core);
  if (!cfg_on("PSXPORT_REPL")) {
    return;
  }
  if (!replQuit_ && replBudget_ <= 0) {
    while ((replBudget_ = game_.repl.read(&core, core.mem_r32(kVblankCounter))) == 0) {
    }
    if (replBudget_ == -2) {
      lucent::info("repl", "end — ending the run cleanly");
      spyro_producer_run_end_now("REPL end");
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
                "t={:.1f}ms vbl={} pace={} present={} ack={} rq_unconsumed={} | site={} quota={} "
                "counter={} rq_n={} unconsumed={}",
                monotonicMilliseconds(),
                fields_,
                paces_,
                presents_,
                acknowledgements_,
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
  const BootSkipAction skip = boot_skip_sample(bootSkip_, startDown);
  if (skip == BootSkipAction::Baseline) {
    lucent::debug("bootskip",
                  "baseline: Start is {} on first boot field; held entry is suppressed",
                  startDown ? "DOWN" : "up");
  } else if (skip == BootSkipAction::AdvancePresentation) {
    const std::uint32_t before = core.mem_r32(kVblankCounter);
    core.mem_w32(kVblankCounter, before + kBootLogoHoldFields);
    lucent::info("bootskip",
                 "fresh Start edge: presentation clock {} -> {} (+{})",
                 before,
                 before + kBootLogoHoldFields,
                 kBootLogoHoldFields);
  }

  const bool guestRootAdvanced = dispatchCallbacks();
  if (!guestRootAdvanced) {
    core.mem_w32(kVblankCounter, core.mem_r32(kVblankCounter) + 1u);
  }
  cadence_.delivered();
  serviceSkipMap(startEdge);
  serviceInspection();

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
  if (request.acknowledgeHostTurn) {
    rec_host_turn_field_delivered(&core);
    ++acknowledgements_;
  }
  for (std::uint32_t eventClass :
       {core.cfg->irqEventClasses[0], core.cfg->irqEventClasses[1], core.cfg->irqEventClasses[2]}) {
    if (eventClass != 0) {
      game_.hle.deliverEvent(eventClass, 0xFFFFFFFFu);
    }
  }

  ++fields_;
  reportField(request, queueSize, queueWasUnconsumed);
  inField_ = false;
  spyro_producer_run_frame(&core);
  return true;
}

void FieldScheduler::fps60CommitDelivered() {
  rec_host_turn_field_delivered(&game_.core);
  ++acknowledgements_;
  lucent::debug("pace",
                "temporal commit ack: vbl={} pace={} present={} ack={}",
                fields_,
                paces_,
                presents_,
                acknowledgements_);
}

FieldScheduler &fieldScheduler(Core &core) {
  return frameDriver(core).fields();
}

const FieldScheduler &fieldScheduler(const Core &core) {
  return frameDriver(core).fields();
}

bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending) {
  return fieldScheduler(core).deliver({.site = site,
                                       .present = !fps60CommitPending,
                                       .pace = !fps60CommitPending,
                                       .acknowledgeHostTurn = !fps60CommitPending});
}

void acknowledgeTemporalCommit(Core &core) {
  fieldScheduler(core).fps60CommitDelivered();
}

void beginBootSkip(Core &core) {
  fieldScheduler(core).bootSkipBegin();
}

void endBootSkip(Core &core) {
  fieldScheduler(core).bootSkipEnd();
}

void observeVblankCallback(Core &core, std::uint32_t function) {
  fieldScheduler(core).observeVblankCallback(function);
}

void hostTurn(Core *core) {
  FieldScheduler &scheduler = fieldScheduler(*core);
  // A host turn may deliver the next guest field while a finite update is still executing, but it
  // is not a second display owner. The enclosing FrameDriver step reaches exactly one presentation
  // fence at its native frame commit; presenting here made that same step advance the fence twice.
  scheduler.deliver(
      {.site = "hostturn", .present = false, .pace = false, .acknowledgeHostTurn = false});
}

} // namespace spyro1
