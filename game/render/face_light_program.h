#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace spyro::face_light {

// Retail's per-face colour program for secondary actors, `func_80020F34` arm `.L80021DB4`
// (external/spyro-1 asm/renderers/r_moby.s). A triangle whose first prefix word has bit 2 set takes
// one colour for all three of its vertices instead of the three material-table colours the ordinary
// path reads, and the arm rejoins that path with nothing else changed. Quads never reach it: bit 2
// on a quad selects the separate billboard program at 0x8002256C.
//
// The colour is a directional term: the cross product of two view-space edges, normalised by its
// own length, scaled by an intensity packed into the control word, and added to a material colour
// packed into the same word. It is computed here rather than on the GTE because a native render
// producer must not mutate guest COP2 state to answer a question about its own geometry.

// The view-space coordinates retail keeps per projected vertex: the RTPS MAC1..3 outputs stored as
// three halfwords at 0x800212C0. The program only differences them and the GTE truncates each
// difference to 16 bits, so the stored width is part of the contract, not a lossy convenience.
struct ViewVertex {
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int16_t z = 0;
};

// D_800770C8 + 0xC / +0x10 / +0x14. The arm splays these three halfwords across all three rows of
// the GTE light-colour matrix, so every row is identical and the term is uncoloured before the
// material background is added.
struct LightColor {
  std::int16_t first = 0;
  std::int16_t second = 0;
  std::int16_t third = 0;
};

// 0x80074B84's reciprocal-magnitude table. The normalise indexes entries 0x40..0xFF, so this is its
// whole reachable extent; an index outside it means the magnitude did not normalise and the face is
// refused rather than read past the table.
inline constexpr std::size_t kMagnitudeEntries = 192;

struct Environment {
  LightColor light{};
  std::span<const std::int16_t> magnitude{};
};

enum class Status : std::uint8_t {
  Ready,         // `color` is the 24-bit value retail gives all three vertices
  NoEnvironment, // no magnitude table was supplied, so the program cannot run
  Additive,      // the control word's top byte selects 0x80021FE0, a different program
  Degenerate,    // the face normal is zero, or its magnitude left the table's range
};

struct Result {
  Status status = Status::NoEnvironment;
  std::uint32_t color = 0;
};

// `control` is the Moby's word at +0x4C, which both record builders copy to the draw record's
// +0x30 and the renderer parks in HI for the whole model.
Result face_color(const std::array<ViewVertex, 3> &view,
                  std::uint32_t control,
                  const Environment &environment);

const char *status_name(Status status);

} // namespace spyro::face_light
