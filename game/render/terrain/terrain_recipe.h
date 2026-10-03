// Deriving the terrain/cyclorama producer's faces (0x8004EBA8) from a guest-free corpus.
//
// WHY IT IS ITS OWN OWNER. This producer draws 90% of everything a Spyro present puts on screen
// that is not already reconstructed between game updates (measured 2026-09-19: 1,335,351 of
// 1,482,522 verbatim items over 1,577 extra presents). A second picture of it cannot be built while
// the deriving code reads guest memory as it goes, because at present time that memory already
// holds the NEXT game update's terrain. So the guest reads move to `terrain_scene` and what remains
// here is pure: model vertices, a transform, a projection and a face list in, screen faces out.
//
// WHAT THE TRANSFORM IS. The guest loads a SHORTMATRIX into the GTE with a ZERO translation and
// bakes each object's world position into its vertex coordinates, through the origin in the object
// header. A frame's camera motion therefore appears in two places at once — the view rotation and
// every object's vertices — and an interval has to sample both. Nothing here reads a projected
// endpoint.
#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace spyro::terrain_recipe {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidInput,
  PoolExhausted,
};

const char *statusName(Status status);

// Retail projects one test point per object through a SEPARATE cull matrix and drops the whole
// object when its view depth does not clear the object's own limit. The rule lives here so the
// capture that decides which objects to read at all and any future check agree on it, but it is
// applied at capture time: an object retail never looked at must not be read, let alone refused on.
bool visible(const psxport::native_projection::FixedAffine &cull,
             psxport::native_projection::ModelVertex point,
             int32_t limit);

// One draw object: the per-frame vertices that place it and the faces that index them. `address` is
// the guest object pointer, which is this layer's instance identity.
struct Object {
  uint32_t address = 0;

  struct Face {
    // The raw guest vertex references, three byte offsets into the object's vertex array. They are
    // validated here rather than at capture because retail validates them itself, in this order,
    // and a capture-time refusal would fire on faces retail rejected by clip first.
    std::array<uint32_t, 3> index{};
    std::array<uint32_t, 3> rgb{};
    // False when one of the face's colour words lies outside RAM. Carried rather than refused at
    // capture, for the same reason: retail only bounds-checks a colour word once the face has
    // survived clip rejection.
    bool colourInRam = true;
    bool gouraud = false;
    uint32_t source = 0; // the guest address of the face's word pair, for a refusal to name
  };

  std::vector<psxport::native_projection::ModelVertex> vertices;
  // False when the object's face table lies outside RAM, in which case `faces` is empty. Retail
  // bounds-checks that table only after the object survives the whole-object clip test, so the
  // refusal belongs at the point in `derive` that reaches it.
  bool faceTableInRam = true;
  std::vector<Face> faces;
};

struct Input {
  // The matrix the guest hands the producer for its vertices, carrying a zero translation — which
  // is why an object's position lives in its vertices rather than here.
  psxport::native_projection::FixedAffine view{};
  psxport::native_projection::ProjectionParams projection{};
  int32_t rightClip = 0;
  // The guest's primitive pool, so the recipe stops where retail's renderer would have run out
  // rather than drawing faces the hardware never had room for.
  uint32_t poolCursor = 0;
  uint32_t poolEnd = 0;
  std::vector<Object> objects;
};

struct Vertex {
  int16_t sx = 0;
  int16_t sy = 0;
  float screenX = 0.0f;
  float screenY = 0.0f;
  float viewZ = 0.0f;
};

struct Face {
  uint32_t object = 0;
  uint32_t source = 0;
  bool gouraud = false;
  std::array<Vertex, 3> vertices{};
  std::array<uint32_t, 3> rgb{};
};

struct Recipe {
  Status status = Status::ValidEmpty;
  uint32_t objects = 0;
  uint32_t candidates = 0;
  uint32_t rejects = 0;
  uint32_t f3 = 0;
  uint32_t g3 = 0;
  uint32_t vertices = 0;
  // Objects whose vertices were sampled across an interval, and objects that had a predecessor but
  // whose interval the framework refused. Both are zero on a logic frame. Reporting them apart
  // matters because `sampled == 0` beside a nonzero `sampleDeclined` is a rule that ran and was
  // refused, which one counter could not tell from a rule that never ran at all.
  uint32_t sampled = 0;
  uint32_t sampleDeclined = 0;
  const char *refusal = "none";
  std::vector<Face> faces;
};

// Where an object's geometry is sampled from. Without one, every object is transformed by the
// input's own view matrix, which is the game update's own picture. With one, an object that has a
// paired predecessor is transformed across the interval between the two frames' view matrices AND
// between the two frames' vertex positions, in view space, before projection — never by blending
// two already-projected vertices.
//
// `previous[i]` is the predecessor of `input.objects[i]`, or nullptr where that object was
// unpaired. Visibility is NOT sampled: the object set is the one the game update resolved, so an
// object cannot appear or vanish partway through an interval.
struct Interval {
  std::span<const Object *const> previous;
  psxport::native_projection::FixedAffine previousView{};
  double t = 1.0;
};

Recipe derive(const Input &input, const Interval *interval = nullptr);

} // namespace spyro::terrain_recipe
