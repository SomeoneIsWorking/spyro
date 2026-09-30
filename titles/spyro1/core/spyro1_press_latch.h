#pragma once

namespace spyro1 {

// Holds one Start/Cross edge from the field it was pressed in to the first place that can act on it.
//
// The pad edge is per-field (FieldOwner::presentationSkipPressed), so a consumer that only looks
// while it is in one phase loses a press made in any other. The latch changes nothing about the
// press: it is neither a timer nor a guest word, and a consumer that never takes it behaves exactly
// as before. One press is taken once.
class PressLatch {
public:
  // Record this field's edge. A field without a press leaves an earlier held press held.
  void observe(bool pressedThisField) {
    held_ = held_ || pressedThisField;
  }

  // True exactly once for each held press.
  bool take() {
    const bool wasHeld = held_;
    held_ = false;
    return wasHeld;
  }

  bool held() const {
    return held_;
  }

private:
  bool held_ = false;
};

} // namespace spyro1
