#include "handoff_store_observer.h"
#include "guest_globals.h"

#include "core.h"
#include "spyro1_field_scheduler.h"

#include <lucent/log.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace spyro1 {
namespace {

using spyro::guest::kGamestate;
using spyro::guest::kGameTick;
using spyro::guest::kLevelTicks;
constexpr std::uint32_t kLevelResetPc = 0x80013698u;
constexpr std::uint32_t kGameResetPc = 0x800136A0u;
constexpr std::uint32_t kStageZeroPc = 0x80013B4Cu;
constexpr std::uint32_t kPadVsyncPc = 0x80053C90u;
constexpr std::uint32_t kGameTickPc = 0x80033A6Cu;

constexpr std::size_t classIndex(HandoffStoreClass sampleClass) {
  return static_cast<std::size_t>(sampleClass);
}

const char *className(HandoffStoreClass sampleClass) {
  switch (sampleClass) {
  case HandoffStoreClass::Transition:
    return "transition";
  case HandoffStoreClass::Bracket:
    return "bracket";
  case HandoffStoreClass::Neighborhood:
    return "neighborhood";
  case HandoffStoreClass::Routine:
    return "routine";
  }
  std::abort();
}

bool isTransition(const HandoffStoreSample &sample) {
  if (sample.guestPc == kLevelResetPc || sample.guestPc == kGameResetPc ||
      sample.guestPc == kStageZeroPc) {
    return true;
  }
  return sample.guestPc == kGameTickPc && sample.after.word != sample.before.word + 1u;
}

HandoffStoreState readState(Core &core, const psx::cpu::StoreObservation &observation) {
  if (!core.mappedMainRamRange(observation.guestPc, 4u)) {
    std::abort();
  }
  const std::uint32_t instruction = core.mem_r32(observation.guestPc);
  if ((instruction >> 26u) != 0x2Bu) {
    std::abort();
  }
  const std::uint32_t base = (instruction >> 21u) & 0x1Fu;
  const std::uint32_t source = (instruction >> 16u) & 0x1Fu;
  const std::int32_t displacement = static_cast<std::int16_t>(instruction);
  const std::uint32_t address = observation.gpr[base] + static_cast<std::uint32_t>(displacement);
  if ((address & 3u) != 0u || !core.mappedMainRamRange(address, 4u)) {
    std::abort();
  }
  return {
      .address = address,
      .word = core.mem_r32(address),
      .source = observation.gpr[source],
      .stage = core.mem_r32(kGamestate),
      .levelTick = core.mem_r32(kLevelTicks),
      .gameTick = core.mem_r32(kGameTick),
      .cycle = observation.guestCycle,
  };
}

} // namespace

HandoffStoreObserver::HandoffStoreObserver(bool enabled, const FieldScheduler &fields)
    : enabled_(enabled), fields_(fields) {}

HandoffStoreObserver::~HandoffStoreObserver() {
  finish();
}

psx::cpu::StoreObserverStatus HandoffStoreObserver::arm(Core &core,
                                                        std::span<const std::uint32_t> targets) {
  if (!enabled_) {
    return psx::cpu::StoreObserverStatus::Configured;
  }
  if (core_ != nullptr) {
    return psx::cpu::StoreObserverStatus::Busy;
  }
  const auto status = core.lightrecExecutor().configureStoreObserver(targets, observe, this);
  if (status == psx::cpu::StoreObserverStatus::Configured) {
    core_ = &core;
    used_ = 0;
    pairs_ = 0;
    neighborhoodRemaining_ = 0;
    bracketOpened_ = false;
    bracketOpen_ = false;
    bracketClosed_ = false;
    pending_ = false;
    classCounts_ = {};
    lastCounts_ = {};
  }
  return status;
}

void HandoffStoreObserver::finish() {
  if (core_ == nullptr) {
    return;
  }
  lastCounts_ = core_->lightrecExecutor().storeObserverReport();
  if (pending_ || core_->lightrecExecutor().configureStoreObserver({}, nullptr, nullptr) !=
                      psx::cpu::StoreObserverStatus::Configured) {
    std::abort();
  }
  core_ = nullptr;
}

void HandoffStoreObserver::observe(const psx::cpu::StoreObservation &observation,
                                   void *context) noexcept {
  static_cast<HandoffStoreObserver *>(context)->capture(observation);
}

void HandoffStoreObserver::capture(const psx::cpu::StoreObservation &observation) noexcept {
  if (core_ == nullptr) {
    std::abort();
  }
  const HandoffStoreState state = readState(*core_, observation);
  if (observation.phase == psx::cpu::StoreObservationPhase::Before) {
    if (pending_) {
      std::abort();
    }
    pending_ = true;
    pendingPc_ = observation.guestPc;
    pendingSample_ = {.guestPc = observation.guestPc, .ordinal = pairs_ + 1u, .before = state};
    const std::string_view site = fields_.activeDeliverySite();
    if (site.size() >= pendingSample_.deliverySite.size()) {
      std::abort();
    }
    std::copy(site.begin(), site.end(), pendingSample_.deliverySite.begin());
    return;
  }
  if (!pending_ || pendingPc_ != observation.guestPc ||
      std::string_view(pendingSample_.deliverySite.data()) != fields_.activeDeliverySite()) {
    std::abort();
  }
  pending_ = false;
  pendingSample_.after = state;
  if (pendingSample_.guestPc == kStageZeroPc && pendingSample_.after.stage == 0u &&
      !bracketOpened_) {
    bracketOpened_ = true;
    bracketOpen_ = true;
  }
  if (isTransition(pendingSample_)) {
    pendingSample_.sampleClass = HandoffStoreClass::Transition;
    neighborhoodRemaining_ = kTransitionNeighborhood;
  } else if (pendingSample_.guestPc == kPadVsyncPc && bracketOpen_) {
    pendingSample_.sampleClass = HandoffStoreClass::Bracket;
  } else if (pendingSample_.guestPc == kPadVsyncPc) {
    pendingSample_.sampleClass = HandoffStoreClass::Routine;
  } else if (neighborhoodRemaining_ > 0u) {
    pendingSample_.sampleClass = HandoffStoreClass::Neighborhood;
    --neighborhoodRemaining_;
  } else {
    pendingSample_.sampleClass = HandoffStoreClass::Routine;
  }
  if (pendingSample_.guestPc == kGameTickPc && bracketOpen_) {
    bracketOpen_ = false;
    bracketClosed_ = true;
  }
  auto &classCount = classCounts_[classIndex(pendingSample_.sampleClass)];
  ++classCount.hits;
  const bool retain = pendingSample_.sampleClass != HandoffStoreClass::Routine ||
                      classCount.recorded < kRoutinePrefix;
  if (retain) {
    // A full interesting capture is a failed discriminator, never a silent lost reset.
    if (used_ == samples_.size()) {
      std::abort();
    }
    samples_[used_++] = pendingSample_;
    ++classCount.recorded;
  } else {
    ++classCount.omitted;
  }
  ++pairs_;
}

std::span<const HandoffStoreSample> HandoffStoreObserver::samples() const {
  return {samples_.data(), used_};
}

psx::cpu::StoreObserverReport HandoffStoreObserver::counts() const {
  if (core_ == nullptr) {
    return lastCounts_;
  }
  return core_->lightrecExecutor().storeObserverReport();
}

HandoffStoreClassCounts HandoffStoreObserver::classCounts(HandoffStoreClass sampleClass) const {
  return classCounts_[classIndex(sampleClass)];
}

std::uint64_t HandoffStoreObserver::omitted() const {
  return pairs_ - used_;
}

bool HandoffStoreObserver::bracketOpened() const {
  return bracketOpened_;
}

bool HandoffStoreObserver::bracketClosed() const {
  return bracketClosed_;
}

void HandoffStoreObserver::report() const {
  if (!enabled_) {
    return;
  }
  const auto counts = this->counts();
  lucent::info("handoff-store",
               "targets={} jit_instructions={} fallback_instructions={} paired={} recorded={} "
               "omitted={} pending={} bracket_opened={} bracket_closed={}",
               counts.targetCount,
               counts.executedJitInstructions,
               counts.fallbackInstructions,
               pairs_,
               used_,
               omitted(),
               pending_ ? 1 : 0,
               bracketOpened_ ? 1 : 0,
               bracketClosed_ ? 1 : 0);
  for (const auto sampleClass : {HandoffStoreClass::Transition,
                                 HandoffStoreClass::Bracket,
                                 HandoffStoreClass::Neighborhood,
                                 HandoffStoreClass::Routine}) {
    const auto classCount = classCounts(sampleClass);
    lucent::info("handoff-store",
                 "class={} hits={} recorded={} omitted={}",
                 className(sampleClass),
                 classCount.hits,
                 classCount.recorded,
                 classCount.omitted);
  }
  for (std::size_t index = 0; index < counts.targetCount; ++index) {
    const auto &target = counts.targets[index];
    lucent::info("handoff-store",
                 "target=0x{:08X} before={} after={} scanned_jit_instructions={}",
                 target.guestPc,
                 target.before,
                 target.after,
                 counts.executedJitInstructions);
  }
  for (std::size_t index = 0; index < used_; ++index) {
    const auto &sample = samples_[index];
    lucent::info("handoff-store",
                 "event={} class={} pc=0x{:08X} address=0x{:08X} source=0x{:08X} "
                 "word=0x{:08X}->0x{:08X} stage={}->{} level_tick={}->{} game_tick={}->{} "
                 "cycle={}->{} site={}",
                 sample.ordinal,
                 className(sample.sampleClass),
                 sample.guestPc,
                 sample.before.address,
                 sample.before.source,
                 sample.before.word,
                 sample.after.word,
                 sample.before.stage,
                 sample.after.stage,
                 sample.before.levelTick,
                 sample.after.levelTick,
                 sample.before.gameTick,
                 sample.after.gameTick,
                 sample.before.cycle,
                 sample.after.cycle,
                 sample.deliverySite[0] != '\0' ? sample.deliverySite.data() : "none");
  }
}

} // namespace spyro1
