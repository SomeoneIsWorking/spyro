#pragma once

#include <cstdint>

namespace spyro1 {

// The retained Spyro frame tail spends at least two display fields per gameplay logic iteration.
// Native rendering supplies those fields through frame_commit; render-suppressed diagnostics must
// use the same quota instead of accidentally running gameplay at twice its retail logic rate.
inline constexpr std::uint32_t kFieldsPerLogicFrame = 2;

// Count fields delivered during one logic iteration. The guest's own frame tail compares the
// previous field stamp and has the same two-field minimum; the host scheduler validates that
// native and diagnostic paths preserve it.
class FieldCadence {
public:
  void beginLogicFrame() {
    fields_ = 0;
  }

  void delivered() {
    ++fields_;
  }

  std::uint32_t fields() const {
    return fields_;
  }

  bool completesLogicFrame() const {
    return fields_ >= kMinimumFieldsPerProductStep;
  }

  static constexpr std::uint32_t kMinimumFieldsPerProductStep = kFieldsPerLogicFrame;

private:
  std::uint32_t fields_ = 0;
};

} // namespace spyro1
