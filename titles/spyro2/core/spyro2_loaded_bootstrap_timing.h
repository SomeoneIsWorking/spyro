#pragma once

#include <cstdint>

namespace spyro2 {

enum class LoadedBootstrapTransition : std::uint8_t {
  None,
  ChildAComplete,
  Threshold164Complete,
  ClearAComplete,
  FixedAComplete,
  ChildBComplete,
  Threshold60Complete,
  ChildCComplete,
  Threshold180AComplete,
  ClearBComplete,
  FixedBComplete,
  ChildDComplete,
  Threshold180BComplete,
  Invalid,
};

// Pure timing owner for loaded US-disc routine 0x80077374(a0=1). The retained payload owns every
// synchronous instruction between these boundaries. This plan owns only the measured libetc timing:
// four calls to child 0x800772A4 contribute twelve VSync(0) fields each, the direct loops compare
// unsigned VSync(-1) snapshots against 164/60/180-field thresholds, and each VSync(4) contributes
// four individually yielded host fields.
class Spyro2LoadedBootstrapTiming final {
public:
  enum class Phase : std::uint8_t {
    ChildA,
    Threshold164,
    ClearA,
    FixedA,
    ChildB,
    Threshold60,
    ChildC,
    Threshold180A,
    ClearB,
    FixedB,
    ChildD,
    Threshold180B,
    Complete,
  };

  void begin(std::uint32_t fieldCounter) {
    phase_ = Phase::ChildA;
    snapshot_ = fieldCounter;
    lastField_ = fieldCounter;
    fieldsInPhase_ = 0u;
    begun_ = true;
  }

  [[nodiscard]] bool begun() const {
    return begun_;
  }

  [[nodiscard]] bool complete() const {
    return begun_ && phase_ == Phase::Complete;
  }

  [[nodiscard]] Phase phase() const {
    return phase_;
  }

  [[nodiscard]] std::uint32_t snapshot() const {
    return snapshot_;
  }

  [[nodiscard]] std::uint32_t nextFieldCallsite() const {
    switch (phase_) {
    case Phase::ChildA:
    case Phase::ChildB:
    case Phase::ChildC:
    case Phase::ChildD:
      return 0x800772F4u;
    case Phase::Threshold164:
      return 0x80077504u;
    case Phase::ClearA:
    case Phase::ClearB:
      return 0x8004C494u;
    case Phase::FixedA:
      return 0x80077524u;
    case Phase::Threshold60:
      return 0x8007769Cu;
    case Phase::Threshold180A:
      return 0x800778E4u;
    case Phase::FixedB:
      return 0x80077944u;
    case Phase::Threshold180B:
      return 0x80077B98u;
    case Phase::Complete:
      return 0u;
    }
    return 0u;
  }

  LoadedBootstrapTransition consumeField(std::uint32_t fieldCounter) {
    if (!begun_ || phase_ == Phase::Complete || fieldCounter != lastField_ + 1u) {
      return LoadedBootstrapTransition::Invalid;
    }
    lastField_ = fieldCounter;
    ++fieldsInPhase_;

    switch (phase_) {
    case Phase::ChildA:
      return finishFixed(12u, Phase::Threshold164, LoadedBootstrapTransition::ChildAComplete);
    case Phase::Threshold164:
      return finishThreshold(164u, Phase::ClearA, LoadedBootstrapTransition::Threshold164Complete);
    case Phase::ClearA:
      return finishFixed(1u, Phase::FixedA, LoadedBootstrapTransition::ClearAComplete);
    case Phase::FixedA:
      if (fieldsInPhase_ == 4u) {
        snapshot_ = fieldCounter;
        return transition(Phase::ChildB, LoadedBootstrapTransition::FixedAComplete);
      }
      return LoadedBootstrapTransition::None;
    case Phase::ChildB:
      return finishFixed(12u, Phase::Threshold60, LoadedBootstrapTransition::ChildBComplete);
    case Phase::Threshold60:
      return finishThreshold(60u, Phase::ChildC, LoadedBootstrapTransition::Threshold60Complete);
    case Phase::ChildC:
      return finishFixed(12u, Phase::Threshold180A, LoadedBootstrapTransition::ChildCComplete);
    case Phase::Threshold180A:
      return finishThreshold(180u, Phase::ClearB, LoadedBootstrapTransition::Threshold180AComplete);
    case Phase::ClearB:
      return finishFixed(1u, Phase::FixedB, LoadedBootstrapTransition::ClearBComplete);
    case Phase::FixedB:
      if (fieldsInPhase_ == 4u) {
        snapshot_ = fieldCounter;
        return transition(Phase::ChildD, LoadedBootstrapTransition::FixedBComplete);
      }
      return LoadedBootstrapTransition::None;
    case Phase::ChildD:
      return finishFixed(12u, Phase::Threshold180B, LoadedBootstrapTransition::ChildDComplete);
    case Phase::Threshold180B:
      return finishThreshold(
          180u, Phase::Complete, LoadedBootstrapTransition::Threshold180BComplete);
    case Phase::Complete:
      return LoadedBootstrapTransition::Invalid;
    }
    return LoadedBootstrapTransition::Invalid;
  }

private:
  LoadedBootstrapTransition transition(Phase next, LoadedBootstrapTransition result) {
    phase_ = next;
    fieldsInPhase_ = 0u;
    return result;
  }

  LoadedBootstrapTransition
  finishFixed(std::uint32_t fields, Phase next, LoadedBootstrapTransition result) {
    return fieldsInPhase_ == fields ? transition(next, result) : LoadedBootstrapTransition::None;
  }

  LoadedBootstrapTransition
  finishThreshold(std::uint32_t fields, Phase next, LoadedBootstrapTransition result) {
    return static_cast<std::uint32_t>(lastField_ - snapshot_) >= fields
               ? transition(next, result)
               : LoadedBootstrapTransition::None;
  }

  Phase phase_ = Phase::ChildA;
  std::uint32_t snapshot_ = 0u;
  std::uint32_t lastField_ = 0u;
  std::uint32_t fieldsInPhase_ = 0u;
  bool begun_ = false;
};

} // namespace spyro2
