#pragma once

#include "lightrec_executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class Core;

namespace spyro1 {

class FieldScheduler;

// Four reached New Game handoff SW instructions, the resident PadVSync level-tick SW, and an
// unreachable control. Adjacent console snapshot PCs are LUI/BEQ, not Lightrec store targets.
inline constexpr std::array<std::uint32_t, 6> kHandoffStoreTargets{
    0x80013698u, 0x800136A0u, 0x80013B4Cu, 0x80053C90u, 0x80033A6Cu, 0xFFFFFFFCu};

struct HandoffStoreState {
  std::uint32_t address = 0;
  std::uint32_t word = 0;
  std::uint32_t source = 0;
  std::uint32_t stage = 0;
  std::uint32_t levelTick = 0;
  std::uint32_t gameTick = 0;
  std::uint32_t cycle = 0;
};

enum class HandoffStoreClass : std::uint8_t {
  Transition,
  Bracket,
  Neighborhood,
  Routine,
};

struct HandoffStoreClassCounts {
  std::uint64_t hits = 0;
  std::uint64_t recorded = 0;
  std::uint64_t omitted = 0;
};

struct HandoffStoreSample {
  std::uint32_t guestPc = 0;
  std::uint64_t ordinal = 0;
  HandoffStoreClass sampleClass = HandoffStoreClass::Routine;
  std::array<char, 32> deliverySite{};
  HandoffStoreState before{};
  HandoffStoreState after{};
};

// Opt-in, title-owned capture at selected translated SW instructions. It reads live RAM while
// Lightrec is paused at the exact instruction; Core's cached PC/registers are not used.
class HandoffStoreObserver final {
public:
  HandoffStoreObserver(bool enabled, const FieldScheduler &fields);
  ~HandoffStoreObserver();
  HandoffStoreObserver(const HandoffStoreObserver &) = delete;
  HandoffStoreObserver &operator=(const HandoffStoreObserver &) = delete;

  psx::cpu::StoreObserverStatus arm(Core &core,
                                    std::span<const std::uint32_t> targets = kHandoffStoreTargets);
  void finish();
  void report() const;

  std::span<const HandoffStoreSample> samples() const;
  psx::cpu::StoreObserverReport counts() const;
  HandoffStoreClassCounts classCounts(HandoffStoreClass sampleClass) const;
  std::uint64_t omitted() const;
  bool bracketOpened() const;
  bool bracketClosed() const;

private:
  static constexpr std::size_t kMaxSamples = 256;
  static constexpr std::size_t kRoutinePrefix = 8;
  static constexpr std::size_t kTransitionNeighborhood = 8;
  static void observe(const psx::cpu::StoreObservation &observation, void *context) noexcept;
  void capture(const psx::cpu::StoreObservation &observation) noexcept;

  bool enabled_ = false;
  const FieldScheduler &fields_;
  Core *core_ = nullptr;
  std::array<HandoffStoreSample, kMaxSamples> samples_{};
  std::array<HandoffStoreClassCounts, 4> classCounts_{};
  std::size_t used_ = 0;
  std::uint64_t pairs_ = 0;
  std::size_t neighborhoodRemaining_ = 0;
  bool bracketOpened_ = false;
  bool bracketOpen_ = false;
  bool bracketClosed_ = false;
  bool pending_ = false;
  std::uint32_t pendingPc_ = 0;
  HandoffStoreSample pendingSample_{};
  psx::cpu::StoreObserverReport lastCounts_{};
};

} // namespace spyro1
