// Pairing one frame's actor records against the frame before them, and sampling the pose between.
//
// WHY IT IS ITS OWN OWNER. Two producers draw compressed-model actors from the same record shape —
// the regular layer at 0x8001F798 and the secondary layer at 0x80020F34 — and an in-between present
// has to answer the identical question for both: which of last frame's records is THIS record, and
// may their poses be blended at all. The answer is a measured rule about the draw record's fields
// (see `Mismatch`), not a per-producer choice, so it is written once. The secondary producer keeps
// its records inside a larger per-actor struct, which is why the pairing takes POINTERS: one
// implementation serves an endpoint that is a flat record vector and one that is not.
//
// PURE. No Core, no queue, no guest memory: a record is already a deep semantic copy.
#pragma once

#include "actor_recipe_capture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::actor_pairing {

// Why a record could not be sampled against the frame before it. `None` is the compatible case;
// every other value names the exact field that differed, because a bare "not interpolated" count
// cannot be told from "this rule was never reached" and cannot say which rule to look at.
//
// Identity here is the model descriptor, the vertex count the sampler indexes by, the depth scale
// the sampled key is expressed in, and the face topology the current frame draws through. Every
// other field of a record is per-endpoint state that the sampler already reads from the side it
// belongs to: each pose decodes its own keyframe streams with its own shifts, each transform is
// built from its own translation and scale byte, and the colours and primitives come from the
// current frame.
//
// The draw record's header word packs two unrelated things. Its top byte is the coordinate shift
// the depth key is expressed in plus the clip-mode sign, and two endpoints in different depth
// scales cannot be sampled against each other. Its middle bytes are the blend factor between the
// model's two keyframe streams, which advances every frame an actor animates. Requiring the whole
// word to match rejected 75,630 of 75,645 incompatible Artisans records — three quarters of every
// actor drawn — for animating rather than for being a different model.
enum class Mismatch : uint8_t {
  None,
  Descriptor,
  VertexCount,
  CoordShift,
  PrimitiveCount,
};
inline constexpr size_t kMismatchCount = (size_t)Mismatch::PrimitiveCount + 1u;

const char *mismatchName(Mismatch mismatch);

struct Census {
  uint32_t actors = 0;       // records in the current endpoint
  uint32_t interpolated = 0; // paired with a compatible predecessor and sampled
  uint32_t unpaired = 0;     // the frame before it drew this instance fewer times, or not at all
  uint32_t incompatible = 0; // a predecessor existed but described a different model
  uint32_t refused = 0;      // the sampler declined, so the record fell back to its own endpoint
  // `incompatible` split by the field that differed, indexed by `Mismatch`. Slot 0 stays zero.
  std::array<uint32_t, kMismatchCount> mismatches{};

  // The field that rejected the most records, or `None` when none were rejected.
  Mismatch worstMismatch() const;
};

// Two records describe the same actor when everything the sampler reads from the current endpoint
// alone is already identical in the previous one. Anything else is a different model reached
// through the same instance pointer, and blending its pose would be meaningless. The named result
// is what the census reports; `compatible` is the same question asked as a predicate.
Mismatch mismatch(const actor_recipe_capture::Record &previous,
                  const actor_recipe_capture::Record &current);
bool compatible(const actor_recipe_capture::Record &previous,
                const actor_recipe_capture::Record &current);

// Sample each record of `current` in place against its counterpart in `previous` at `t`. Pairing is
// by Moby instance, in occurrence order within one frame. An unpaired, incompatible or declined
// record keeps the pose its own frame built, which is what this logic frame shows anyway and is
// strictly closer than replaying the previous frame's picture for it.
void sample(std::span<const actor_recipe_capture::Record *const> previous,
            std::span<actor_recipe_capture::Record *const> current,
            double t,
            Census &census);

// The same pairing for a producer whose endpoint is a flat record vector.
void sample(const std::vector<actor_recipe_capture::Record> &previous,
            std::vector<actor_recipe_capture::Record> &current,
            double t,
            Census &census);

} // namespace spyro::actor_pairing
