#pragma once

#include <cstdint>

namespace spyro1 {

// Count fields delivered during one logic iteration. The renderer owns the retail >=2-field
// throttle against the previous frame's guest stamp; a host turn can advance that stamp during
// update, and the first gameplay frame begins after hundreds of boot fields. The driver therefore
// validates that every product step reached a field boundary, rather than inventing a second,
// per-iteration two-call rule. A guest-suppressed render still needs one host-owned visible field;
// suppression means reuse the previous picture, not omit the product presentation fence.
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

  static constexpr std::uint32_t kMinimumFieldsPerProductStep = 1;

private:
  std::uint32_t fields_ = 0;
};

} // namespace spyro1
