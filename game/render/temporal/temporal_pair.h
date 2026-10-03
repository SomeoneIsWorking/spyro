// class Pair — the two-endpoint lifecycle every temporal source in this title shares.
//
// WHAT IT OWNS. A temporal source reconstructs an in-between present from the logic frame before
// the current one, so it needs exactly this: one payload retained per logic frame, at most once;
// the previous payload rotated forward at present time; and admission only when the two payloads
// really are consecutive frames of one scene, produced while interpolation was active and this was
// not the reference leg. Nothing here knows what a payload IS.
//
// WHY IT IS ONE CLASS. The world source and the regular-actor source each grew their own copy of
// this state machine, character for character — the same seven members, the same reset rules, the
// same off-by-one in `previous_->serial + 1 == current_->serial`. The secondary-actor source would
// have been the third. Two copies that agree are one silent drift away from two answers to "is this
// pair admissible", which is the question presentation depends on.
//
// THE SERIAL LIVES HERE, NOT IN THE PAYLOAD. It is frame bookkeeping, not scene data: a payload
// that carried its own serial could be retained with the wrong one, and the consecutive-frame test
// is the whole admission rule.
//
// REFUSING IS STICKY FOR THE FRAME, AND THAT IS THE POINT. A source that submits twice in one logic
// frame composes ONE picture; reconstructing half of it would be worse than reconstructing none. So
// a refusal drops the current payload and cannot be undone until the next `begin`.
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace spyro::temporal {

template <class Payload> class Pair {
public:
  // Open a logic frame. Any payload from a different scene, or from a run where interpolation was
  // not active, is dropped rather than paired across the discontinuity.
  void begin(uint64_t scene, bool reference, bool active) {
    ++serial_;
    eligible_ = false;
    current_.reset();
    retained_ = false;
    refused_ = false;
    const bool enabled = active && !reference;
    if (!enabled || enabled != active_ || scene != scene_) {
      previous_.reset();
    }
    scene_ = scene;
    active_ = enabled;
  }

  // Accept this frame's payload. Returns false without retaining when the source is inactive, and
  // refuses the frame outright on a second retain (see the class note).
  bool retain(Payload payload) {
    if (!active_) {
      return false;
    }
    if (retained_ || refused_) {
      refuse();
      return false;
    }
    retained_ = true;
    current_ = std::move(payload);
    currentSerial_ = serial_;
    return true;
  }

  // Drop this frame's payload and keep it dropped until the next `begin`.
  void refuse() {
    current_.reset();
    eligible_ = false;
    refused_ = true;
  }

  // Present time: this frame's payload becomes the next frame's predecessor.
  void rotate() {
    previous_ = std::move(current_);
    previousSerial_ = currentSerial_;
    current_.reset();
    eligible_ = false;
  }

  // Two payloads from consecutive frames of one active, non-reference scene.
  bool paired() const {
    return active_ && !refused_ && previous_ && current_ && previousSerial_ + 1 == currentSerial_;
  }

  bool active() const {
    return active_;
  }
  bool retained() const {
    return retained_;
  }
  bool refused() const {
    return refused_;
  }
  uint64_t serial() const {
    return serial_;
  }
  const Payload *previous() const {
    return previous_ ? &*previous_ : nullptr;
  }
  const Payload *current() const {
    return current_ ? &*current_ : nullptr;
  }
  Payload *mutableCurrent() {
    return current_ ? &*current_ : nullptr;
  }
  // A source whose endpoints are completed after retention — the world source materialises the
  // resources its predecessor was still waiting for — edits both in place. Editing is not
  // retaining: neither the serials nor the refusal state move.
  Payload *mutablePrevious() {
    return previous_ ? &*previous_ : nullptr;
  }

  // Admission, decided once per frame by the owning source's own preflight and read by the scene.
  // It is a decision ABOUT the pair rather than part of its lifecycle, which is why it is set from
  // outside; `begin`, `refuse` and `rotate` clear it because none of them leaves a pair to admit.
  bool eligible() const {
    return eligible_;
  }
  void admit(bool eligible) {
    eligible_ = eligible;
  }

private:
  std::optional<Payload> previous_;
  std::optional<Payload> current_;
  uint64_t serial_ = 0;
  uint64_t previousSerial_ = 0;
  uint64_t currentSerial_ = 0;
  uint64_t scene_ = 0;
  bool active_ = false;
  bool retained_ = false;
  bool refused_ = false;
  bool eligible_ = false;
};

} // namespace spyro::temporal
