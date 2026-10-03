#pragma once

#include <cstdint>
#include <span>

// The arm at 0x80024B60 that the paired-actor renderer takes when the high byte of g_Spyro+0x28 is
// set. It was recorded as a separate "alternate/status-plane parser" and refused on sight; it is
// not a parser at all. The renderer runs every entry of the model's colour table through one GTE
// DPCS toward a far colour packed in the same control word, writes the results to a scratch table,
// and points the ORDINARY primitive parser at that copy instead. Nothing downstream changes: the
// same stream, the same offsets, the same commands, different colours.
//
// So this owner is one pure table-to-table transform, and the port applies it where the guest does
// — to the material table, before it is handed to the resolver — rather than carrying the control
// word through the decoder as a mode.
namespace spyro::paired_actor_color_fade {

// Whether the control word selects the fade at all. A zero high byte is the ordinary path, and the
// renderer skips the whole transform rather than running an identity over the table.
bool active(uint32_t control);

// Fade every entry in place. Each word keeps its own code byte: the guest's result carries the
// GTE's CODE register instead, which no consumer of this table reads — every one of them masks to
// the low 24 bits. Does nothing when the control word does not select the fade.
void apply(uint32_t control, std::span<uint32_t> materials);

} // namespace spyro::paired_actor_color_fade
