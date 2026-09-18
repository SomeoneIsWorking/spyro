// The secondary-actor layer's temporal source: the 0x80020F34 producer's scene frame, one endpoint
// per logic frame, reconstructed at any point between the last two.
//
// It owns nothing the regular layer already owns. The endpoint lifecycle and admission rule are
// `spyro::temporal::Pair`; pairing this frame's records against the frame before them, and the
// measured identity rule that decides whether two poses may be blended at all, are
// `spyro::actor_pairing`; the route from a ready recipe to the queue is `actor_submission`, reached
// through this layer's own `secondary_actor_emit`.
//
// THE ENDPOINT IS THE WHOLE SCENE FRAME, not just its records. The secondary recipe reads the
// frame's lighting control word per record and its shadow list, so a reconstruction has to derive
// from a frame, not from a record corpus. The retained copy costs one deep copy per logic frame and
// holds no guest addresses that could be invalidated underneath it.
#pragma once

#include "actor_pairing.h"
#include "actor_stage.h"
#include "secondary_actor_scene.h"
#include "temporal_pair.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::secondary_actor_temporal {

using Endpoint = secondary_actor_scene::Frame;

using Status = actor_stage::Temporal;

// Samples `current`'s records in place against `previous`'s at `t`, by Moby instance. Pure: no
// Core, no queue, no guest state.
void sample(const Endpoint &previous, Endpoint &current, double t, actor_pairing::Census &census);

class History {
public:
  void begin(uint64_t scene, bool reference, bool active) {
    pair_.begin(scene, reference, active);
  }
  void retain(Endpoint frame) {
    pair_.retain(std::move(frame));
  }
  void refuse() {
    pair_.refuse();
  }
  void rotate() {
    pair_.rotate();
  }
  bool paired() const {
    return pair_.paired();
  }
  Status emit(Core &core, RenderQueue &target, double t, actor_pairing::Census &census) const;
  uint64_t frameSerial() const {
    return pair_.serial();
  }
  const Endpoint *previous() const {
    return pair_.previous();
  }
  const Endpoint *current() const {
    return pair_.current();
  }
  bool eligible() const {
    return pair_.eligible();
  }
  void admit(bool eligible) {
    pair_.admit(eligible);
  }

private:
  temporal::Pair<Endpoint> pair_;
};

} // namespace spyro::secondary_actor_temporal
