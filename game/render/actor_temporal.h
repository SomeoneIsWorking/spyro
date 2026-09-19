// The regular-actor layer's temporal source: the 0x8001F798 producer's records, one endpoint per
// logic frame, reconstructed at any point between the last two.
//
// The two questions this does NOT answer itself, because they are not this layer's: the endpoint
// lifecycle and admission rule live in `spyro::temporal::Pair`, and pairing one frame's records
// against the frame before them lives in `spyro::actor_pairing` — the secondary-actor layer asks
// both of the same owners.
#pragma once

#include "actor_emit.h"
#include "actor_pairing.h"
#include "actor_recipe_capture.h"
#include "actor_stage.h"
#include "temporal_pair.h"

#include <cstdint>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::actor_temporal {

// The record corpus one logic frame's regular-actor producer published. Records are the producer's
// own capture, so an endpoint costs one deep copy per frame and stays in exactly the semantic form
// the prefix builder consumes. Nothing here is a guest address or a projected result.
using Endpoint = std::vector<actor_recipe_capture::Record>;

using Status = actor_stage::Temporal;

// One owner for endpoint lifetime, scene continuity, and reconstruction. Recipes are rebuilt for
// every sample; they are never retained as interpolation inputs.
class History {
public:
  // Named so a shared reconstruction can declare the census its layer reports into.
  using Census = actor_pairing::Census;

  void begin(uint64_t scene, bool reference, bool active) {
    pair_.begin(scene, reference, active);
  }
  void retain(Endpoint records) {
    pair_.retain(std::move(records));
  }
  void refuse() {
    pair_.refuse();
  }
  void rotate() {
    pair_.rotate();
  }
  // A complete consecutive pair in one scene. Nothing here reads guest memory: an actor record is
  // a deep copy, so unlike the world source it cannot be invalidated underneath the history.
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

} // namespace spyro::actor_temporal
