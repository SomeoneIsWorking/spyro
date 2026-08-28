#pragma once

#include "world_chunk_codec.h"

#include <cstdint>
#include <vector>

// world_animation — RenderWorldChunks' phase-1 per-sector animation, owned as a PLAN.
//
// The guest renderer 0x800258F0 does not only select and cull sectors: for every sector it keeps,
// it also advances up to four animation channels that WRITE BACK into that sector's own vertex and
// colour arrays (0x80025BAC..0x800261A0). Phase 2 then projects the arrays the channels just wrote,
// so a producer that skips the animation is reading last-frame geometry, and one that refuses is
// simply blind on any sector whose channel is live. Both are wrong; this owns the step.
//
// The decode is PURE: it reads guest RAM and yields the exact ordered writes the guest would make.
// Committing them is a separate, explicit act (`apply`), so the read-only recipe producers keep
// their contract and the one place that mutates guest state is named.
//
// The four channels differ only in where they write, and each has a direct and an interpolated
// form (the guest picks by the keyframe's blend factor):
//   0  LQ vertices  packed 11/11/10, dest sector+28
//   1  LQ colours   delta-indexed, dest sector+28 + vertexCount*4
//   2  HQ vertices  packed 11/11/10, dest sector+28 + [sector+23]*4
//   3  HQ colours   delta-indexed, two interleaved streams from the [sector+20] layout word
// The interpolated forms run the GTE — INTPL for vertices, DPCS for colours — so this file carries
// exact transcriptions of those two operations rather than an approximation of them.
namespace spyro::world_animation {

struct Write {
  uint32_t address = 0;
  uint32_t value = 0;
  uint8_t width = 4; // 4 = word, 1 = the channel's stamp byte
};

struct Plan {
  std::vector<Write> writes;
  uint32_t channels = 0; // channels decoded, over every sector in the selection
  uint32_t direct = 0;   // of those, how many took the straight-copy form
  uint32_t blended = 0;  // ...and how many the GTE-interpolated form
};

// Decodes one sector's active channels into `plan`. `active` is the guest's own combined word:
// the sector's four stamp bytes ORed with the mask naming which quality halves were emitted. A
// channel runs when its byte is below 0x80. Returns false and names the offending structure in
// `why` when the animation data is not addressable; a partial plan is never committed.
bool appendSector(const world_chunk_codec::RamView &ram,
                  uint32_t sector,
                  uint32_t active,
                  Plan &plan,
                  const char *&why);

// The two GTE operations the interpolated channels use, transcribed from the hardware reference so
// the blended forms are exact rather than close. sf=1, lm=0, which is what the guest encodes.
struct Vector3 {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};
Vector3 intpl(Vector3 ir, Vector3 farColor, int32_t ir0);
uint32_t dpcs(uint32_t rgb, Vector3 farColor, int32_t ir0);

} // namespace spyro::world_animation
