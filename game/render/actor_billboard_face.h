#pragma once

#include "native_projection.h"

#include <cstdint>

namespace spyro::actor_billboard {

// The camera-facing textured sprite that bit 2 of a QUAD's prefix word selects: `0x800205C4` in the
// regular actor renderer `0x8001F798`, `.L8002256C` in the secondary `0x80020F34`. It is not a
// four-vertex face at all — it projects ONE model vertex, re-runs the GTE's depth divide to get a
// scale for that distance, and lays four screen-aligned corners around the projected centre. The
// recipe refused it as `Reason::Ft4` until this owner existed.
//
// The half-extents are packed in the material word beside the single colour offset the arm reads.
struct Extents {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
};

// The arm's own depth cue: DQA is forced to 0x100 and DQB to zero for this one divide, and the
// rotation is zeroed so MAC3 — and therefore SZ3 — is the centre's stored depth unchanged. IR0 is
// taken from MAC0 through a 16-bit register write, as the guest's `mfc2/srl/mtc2` pair does, so a
// cue past 1000h keeps the value hardware's own RTPS clamp would have discarded.
Extents extents(const psxport::native_projection::ProjectionParams &projection,
                const psxport::native_projection::NativeProjectedVertex &centre,
                uint32_t material);

// The half-extents this arm reads out of the material word, exposed so a test can state them
// without restating the shifts.
constexpr int32_t halfWidth(uint32_t material) {
  return (int32_t)((material >> 10) & 0x1ffu);
}

constexpr int32_t halfHeight(uint32_t material) {
  return (int32_t)((material >> 1) & 0x1ffu);
}

} // namespace spyro::actor_billboard
