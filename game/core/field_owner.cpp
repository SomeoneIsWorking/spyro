#include "field_owner.h"

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
#include "spyro_context.h"
#include "vblank_irq.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <lucent/log.h>

namespace spyro {
namespace {

constexpr std::uint16_t kPadStart = 0x0008u;
constexpr std::uint16_t kPadCross = 0x4000u;
constexpr std::uint32_t kStackPoison = 0xCDCDCDCDu;

} // namespace

FieldOwner::FieldOwner(Game &game, FieldOwnerFacts facts, FieldObserver *observer)
    : game_(game), facts_(facts), observer_(observer) {
  cadence_.setMinimumFieldsPerProductStep(facts_.fieldsPerLogicFrame);
}

void FieldOwner::publish() {
  spyro_context(game_.core).fieldOwner = this;
}

std::int32_t FieldOwner::counter() const {
  return static_cast<std::int32_t>(game_.core.mem_r32(facts_.fieldCounter));
}

std::string_view FieldOwner::activeDeliverySite() const {
  return inField_ && activeDeliverySite_ != nullptr ? std::string_view(activeDeliverySite_)
                                                    : std::string_view{};
}

void FieldOwner::armHostClock() {
  if (hostClockArmed_) {
    lucent::error("fields", "{} host field clock armed twice", facts_.titleName);
    std::abort();
  }
  hostClockArmed_ = true;
  psx::cpu::registerHostTurn(
      game_.core, &FieldOwner::hostTurnThunk, gpu_field_rate_millihz(&game_.core));
  lucent::info("fields", "{} host field clock armed at field {}", facts_.titleName, counter());
}

void FieldOwner::observeVblankCallback(std::uint32_t function) {
  if (function == callbackFallback_) {
    return;
  }
  callbackFallback_ = function;
  lucent::info("fields",
               "{} VSyncCallback(0x{:08X}) registered for host-owned field delivery",
               facts_.titleName,
               function);
}

void FieldOwner::armHandlerStack(Core &core) {
  const std::uint32_t floor = facts_.handlerStackTop - facts_.handlerStackBytes;
  for (std::uint32_t address = floor; address < facts_.handlerStackTop; address += 4) {
    core.mem_w32(address, kStackPoison);
  }
  lucent::info("fields",
               "{} guest vblank callbacks use IRQ stack [0x{:08X},0x{:08X})",
               facts_.titleName,
               floor,
               facts_.handlerStackTop);
}

bool FieldOwner::dispatchCallbacks() {
  Core &core = game_.core;
  const std::uint32_t root =
      facts_.rootHandlerSlot != 0 ? core.mem_r32(facts_.rootHandlerSlot) : 0u;
  const std::uint32_t target = root != 0 ? root : callbackFallback_;
  if (target == 0) {
    return false;
  }
  const bool rootOwnsIrq = root != 0 && game_.hle.exception_exit_buf != 0;
  if (rootOwnsIrq && (!spyro::hasPendingEnabledVblank(core.irqStatLatch(), game_.hle.i_mask) ||
                      !game_.hle.canDispatchInterrupt(core))) {
    // An installed guest IRQ continuation retains counter ownership while its edge is masked or
    // its CPU context is unavailable. Calling the root directly here would duplicate that edge
    // when the runtime later services it. Leave both hardware state and pending work untouched.
    return true;
  }
  if (!handlerStackArmed_) {
    handlerStackArmed_ = true;
    armHandlerStack(core);
  }

  const std::int32_t before = counter();
  R3000 saved = static_cast<R3000 &>(core);
  core.r[29] = facts_.handlerStackTop;
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

  if (core.mem_r32(facts_.handlerStackTop - facts_.handlerStackBytes) != kStackPoison) {
    lucent::error("fields",
                  "{} guest vblank callback overflowed its {}-byte IRQ stack at 0x{:08X}",
                  facts_.titleName,
                  facts_.handlerStackBytes,
                  facts_.handlerStackTop - facts_.handlerStackBytes);
    std::abort();
  }

  // A root actually dispatched here must tick exactly once. Masked/critical guest IRQ ownership
  // returned above: physical fields continue, but their counter tick waits for IRQ delivery.
  const std::int32_t after = counter();
  if (root != 0 && after != before + 1) {
    lucent::error("fields",
                  "{} guest vblank root advanced counter {} -> {}; host scheduler requires exactly "
                  "one field",
                  facts_.titleName,
                  before,
                  after);
    std::abort();
  }
  return root != 0;
}

void FieldOwner::serviceRepl() {
  Core &core = game_.core;
  if (!cfg_on("PSXPORT_REPL")) {
    return;
  }
  if (!replQuit_ && replBudget_ <= 0) {
    while ((replBudget_ = game_.repl.read(&core, static_cast<std::uint32_t>(counter()))) == 0) {
    }
    if (replBudget_ == -2) {
      lucent::info("repl", "end — ending the run cleanly");
      runtimeRun(core).requestEnd();
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

void FieldOwner::reportField(const FieldRequest &request, int queueSize, bool queueWasUnconsumed) {
  const double elapsedMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - epoch_).count();
  lucent::debug("pace",
                "t={:.1f}ms vbl={} pace={} present={} rq_unconsumed={} refused={} | site={} "
                "quota={} counter={} rq_n={} unconsumed={}",
                elapsedMs,
                fields_,
                paces_,
                presents_,
                queueWasUnconsumed ? 1 : 0,
                refused_,
                request.site,
                game_.core.cfg ? game_.core.cfg->paceQuota : 0u,
                counter(),
                queueSize,
                queueWasUnconsumed ? 1 : 0);
}

bool FieldOwner::deliver(FieldRequest request) {
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
  // full-console reference parks too. A hold committed at the park is then seen by this field's
  // SIO exchange, and RAM read at the park is the same guest phase on both cores.
  serviceRepl();

  const int queueSize = game_.rq.n;
  const bool queueWasUnconsumed = queueSize > 0 && !game_.rq.consumed;
  if (request.present) {
    game_.rq.flush(&core);
  }

  game_.pad.serviceFrame();
  const bool startDown = (game_.pad.buttons & kPadStart) == 0;
  const bool startEdge = startDown && (previousButtons_ & kPadStart) != 0;
  previousButtons_ = game_.pad.buttons;
  if (!game_.timing.advanceDisplayFields(1, 1, gpu_field_rate_millihz(&core))) {
    lucent::error(
        "fields", "{} display clock refused one field at {}", facts_.titleName, request.site);
    std::abort();
  }
  const bool guestRootOwnsCounter = dispatchCallbacks();
  if (!guestRootOwnsCounter && facts_.fieldCounter != 0) {
    core.mem_w32(facts_.fieldCounter, core.mem_r32(facts_.fieldCounter) + 1u);
  }
  cadence_.delivered();
  if (observer_ != nullptr) {
    observer_->onField(core, startEdge);
  }
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
  runtimeRun(core).fieldDelivered();
  return true;
}

bool FieldOwner::presentationSkipPressed() const {
  return game_.pad.pressedButton(kPadStart | kPadCross);
}

void FieldOwner::hostTurnThunk(Core *core) {
  // A host turn may deliver the next guest field while a finite update is still executing, but it
  // is not a second display owner. The enclosing FrameDriver step reaches exactly one presentation
  // fence at its own commit; presenting here made that same step advance the fence twice.
  fieldOwner(*core).deliver({.site = "hostturn", .present = false, .pace = false});
}

FieldOwner &fieldOwner(Core &core) {
  FieldOwner *owner = spyro_context(core).fieldOwner;
  if (owner == nullptr) {
    lucent::error("fields", "no title field owner is published for this Core");
    std::abort();
  }
  return *owner;
}

const FieldOwner &fieldOwner(const Core &core) {
  const FieldOwner *owner = spyro_context(core).fieldOwner;
  if (owner == nullptr) {
    lucent::error("fields", "no title field owner is published for this Core");
    std::abort();
  }
  return *owner;
}

bool deliverNativeField(Core &core, const char *site, bool fps60CommitPending) {
  return fieldOwner(core).deliver(
      {.site = site, .present = !fps60CommitPending, .pace = !fps60CommitPending});
}

} // namespace spyro
