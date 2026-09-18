#pragma once

#include "actor_emit.h"
#include "actor_recipe_capture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::actor_temporal {

// The record corpus one logic frame's regular-actor producer published. Records are the producer's
// own capture, so an endpoint costs one deep copy per frame and stays in exactly the semantic form
// the prefix builder consumes. Nothing here is a guest address or a projected result.
struct Endpoint {
  std::vector<actor_recipe_capture::Record> records;
  uint64_t serial = 0;
};

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

enum class Status : uint8_t { Ready, ValidEmpty, NoEndpoints, Recipe, Submission, DrawArea };

const char *statusName(Status status);

// Two records describe the same actor when everything the sampler reads from the current endpoint
// alone is already identical in the previous one. Anything else is a different model reached
// through the same instance pointer, and blending its pose would be meaningless. The named result
// is what the census reports; `compatible` is the same question asked as a predicate.
Mismatch mismatch(const actor_recipe_capture::Record &previous,
                  const actor_recipe_capture::Record &current);
bool compatible(const actor_recipe_capture::Record &previous,
                const actor_recipe_capture::Record &current);

// Samples `current` against `previous` at `t` into `sampled`. Pure: no Core, no queue, no guest
// state. Pairing is by Moby instance. An unpaired, incompatible or declined record is emitted at
// its current endpoint, which is what this logic frame shows anyway and is strictly closer than
// replaying the previous frame's picture for it.
void sample_records(const Endpoint &previous,
                    const Endpoint &current,
                    double t,
                    std::vector<actor_recipe_capture::Record> &sampled,
                    Census &census);

// One owner for endpoint lifetime, scene continuity, and reconstruction. Recipes are rebuilt for
// every sample; they are never retained as interpolation inputs.
class History {
public:
  void begin(uint64_t scene, bool reference, bool active);
  void retain(std::vector<actor_recipe_capture::Record> records);
  void refuse();
  void rotate();
  // A complete consecutive pair in one scene. Nothing here reads guest memory: an actor record is
  // a deep copy, so unlike the world source it cannot be invalidated underneath the history.
  bool paired() const;
  Status emit(Core &core, RenderQueue &target, double t, Census &census) const;
  uint64_t frameSerial() const {
    return serial_;
  }
  const Endpoint *previous() const {
    return previous_ ? &*previous_ : nullptr;
  }
  const Endpoint *current() const {
    return current_ ? &*current_ : nullptr;
  }
  bool eligible = false;

private:
  std::optional<Endpoint> previous_;
  std::optional<Endpoint> current_;
  uint64_t serial_ = 0;
  uint64_t scene_ = 0;
  bool active_ = false;
  bool seen_ = false;
  bool refused_ = false;
};

} // namespace spyro::actor_temporal
