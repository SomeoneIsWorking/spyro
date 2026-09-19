// The world-shaded sprite queue's temporal source: the 0x80022A2C producer's scene input, one
// endpoint per logic frame, reconstructed at any point between the last two.
//
// It owns nothing another layer already owns. The endpoint lifecycle and admission rule are
// `spyro::temporal::Pair`; matching this frame's records to the frame before them is
// `spyro::instance_pairing`; the route from an input to the queue is `field_shaded_queue_emit`,
// which the logic frame takes as well.
//
// THE ENDPOINT IS THE RECIPE'S INPUT, NOT THE SCENE FRAME. The rest of a scene frame is guest-state
// bookkeeping — the shadow cursor, the visited and transformed actor lists, the source census —
// which a reconstruction never commits and never reads. Retaining the input alone is the whole of
// what a second picture of this layer needs, and it holds no guest addresses that could be
// invalidated underneath it.
//
// WHY THE SAMPLING IS NOT THE ACTOR LAYERS'. A compressed-model actor is sampled by rebuilding its
// pose from two keyframe streams, which is what `actor_pairing` does. A shaded-queue record carries
// its mesh already decoded, so its interval is between two transforms over one vertex corpus and is
// applied inside the recipe, in view space, before projection. Only the pairing is shared.
#pragma once

#include "actor_stage.h"
#include "field_shaded_queue_recipe.h"
#include "instance_pairing.h"
#include "temporal_pair.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::field_shaded_queue_temporal {

using Endpoint = field_shaded_queue_recipe::Input;

// Why a record could not be sampled against the frame before it. `None` is the compatible case;
// every other value names the exact field that differed, because a bare "not interpolated" count
// cannot be told from "this rule was never reached" and cannot say which rule to look at.
//
// Identity here is the mesh the record draws, the vertex corpus the interval indexes by, and the
// face topology and clip mode the current frame draws through. Everything else in a record is
// per-endpoint state the recipe reads from the current frame alone: the transform is the thing
// being sampled, and the lighting entry and vertex colours come from the frame being drawn.
enum class Mismatch : uint8_t {
  None,
  MeshIndex,
  VertexCount,
  PrimitiveCount,
  ClipMode,
};
inline constexpr size_t kMismatchCount = (size_t)Mismatch::ClipMode + 1u;

const char *mismatchName(Mismatch mismatch);

using Census = instance_pairing::ReasonedCensus<Mismatch, kMismatchCount>;
using Status = actor_stage::Temporal;

// Two records describe the same draw when everything the interval does not sample is already
// identical. Anything else is a different mesh reached through the same actor pointer, and blending
// its transform onto this frame's vertices would be meaningless.
Mismatch mismatch(const field_shaded_queue_recipe::Record &previous,
                  const field_shaded_queue_recipe::Record &current);

// The predecessor of each of `current`'s records, parallel to `current.records`, with nullptr where
// a record was unpaired or its predecessor was rejected. Pure: no Core, no queue, no guest state.
std::vector<const field_shaded_queue_recipe::Record *>
pair(const Endpoint &previous, const Endpoint &current, Census &census);

class History {
public:
  using Census = field_shaded_queue_temporal::Census;

  void begin(uint64_t scene, bool reference, bool active) {
    pair_.begin(scene, reference, active);
  }
  void retain(Endpoint input) {
    pair_.retain(std::move(input));
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
  Status emit(Core &core, RenderQueue &target, double t, Census &census) const;
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

} // namespace spyro::field_shaded_queue_temporal
