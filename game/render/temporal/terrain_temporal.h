// The terrain producer's temporal source: the 0x8004EBA8 producer's captured corpus, one endpoint
// per game update, reconstructed at any point between the last two.
//
// WHY THIS LAYER MATTERS MOST. Measured over a 7,200-field Artisans replay, this producer owned
// 1,335,351 of the 1,482,522 items still replayed verbatim in an in-between present — 90.1%. Every
// other unowned producer put together drew about thirty faces per game update.
//
// It owns nothing another layer already owns. The endpoint lifecycle and the consecutive-update
// admission rule are `spyro::temporal::Pair`; matching this update's objects to the update before
// them is `spyro::instance_pairing`; the route from a corpus to the queue is `terrain_emit`, which
// the game update takes as well.
//
// WHERE TERRAIN'S MOTION LIVES. The guest loads its view matrix with a zero translation and bakes
// each object's world position into its vertex coordinates through the origin in the object header.
// A frame's camera motion therefore appears in two places at once, and the interval samples both:
// the view rotation is one endpoint pair, and each object's model vertices are another. Sampling
// only the matrix would hold the world still while the camera turned.
#pragma once

#include "actor_stage.h"
#include "instance_pairing.h"
#include "temporal_pair.h"
#include "terrain_recipe.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::terrain_temporal {

using Endpoint = terrain_recipe::Input;

// Why an object could not be sampled against the update before it. `None` is the compatible case;
// every other value names the exact field that differed, because a bare "not interpolated" count
// cannot be told from "this rule was never reached" and cannot say which rule to look at.
//
// Identity here is the mesh: the vertex corpus the interval indexes by, and the face list drawn
// over it. Everything else in an object is per-endpoint state the recipe reads from the update
// being drawn.
enum class Mismatch : uint8_t {
  None,
  VertexCount,
  FaceCount,
};
inline constexpr size_t kMismatchCount = (size_t)Mismatch::FaceCount + 1u;

const char *mismatchName(Mismatch mismatch);

using Census = instance_pairing::ReasonedCensus<Mismatch, kMismatchCount>;
using Status = actor_stage::Temporal;

// Two objects describe the same terrain when the mesh the interval indexes is the same. Anything
// else is different geometry reached through the same guest object pointer, and sampling one
// update's vertices against the other's would be meaningless.
Mismatch mismatch(const terrain_recipe::Object &previous, const terrain_recipe::Object &current);

// The predecessor of each of `current`'s objects, parallel to `current.objects`, with nullptr where
// an object was unpaired or its predecessor was rejected. Pure: no Core, no queue, no guest state.
std::vector<const terrain_recipe::Object *>
pair(const Endpoint &previous, const Endpoint &current, Census &census);

class History {
public:
  using Census = terrain_temporal::Census;

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

} // namespace spyro::terrain_temporal
