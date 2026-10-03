#pragma once

#include "world_animation.h"
#include "world_chunk_codec.h"
#include "world_source.h"

#include <cstdint>

// world_source_animation — advance a sector's animation channel inside an owned Source, never in
// guest RAM.
//
// Two owners need the geometry a channel WOULD write without letting the guest see the write:
//   * the temporal history, materialising a channel that becomes visible only in an interpolated
//     sample between two retained endpoints;
//   * the widescreen draw, for a sector only the widened view admits. Retail at 4:3 never animates
//     such a sector, so its guest arrays and stamp bytes must stay exactly as they are (issue
//     0152), yet the port still has to draw it with the geometry its channel describes.
// Both take the one decoded plan and apply it to the Source's decoded chunk, then mark the channel
// consumed in the Source's own header copy so the refusing preparation accepts it.
namespace spyro::world_source_animation {

// Applies one decoded channel (`plan.channels == 1`, exactly one stamp write) of sector `index` at
// guest `address` to `source`. A refusal names the offending structure in `why` and may leave the
// Source partly written, so every caller discards (or never adopts) a Source this refused on.
bool applyChannel(world_source::Source &source,
                  uint8_t index,
                  uint32_t address,
                  uint32_t channel,
                  const world_animation::Plan &plan,
                  const char *&why);

// Decodes every live channel of every sector the drawn view admits but retail's 4:3 cull does not,
// from the current guest RAM, into `source`. A no-op at 4:3, where no sector is margin-only.
bool animateDrawnOnly(const world_chunk_codec::RamView &ram,
                      world_source::Source &source,
                      const char *&why);

} // namespace spyro::world_source_animation
