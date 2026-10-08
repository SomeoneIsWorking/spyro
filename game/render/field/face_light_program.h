#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace spyro::face_light {

// Retail's two per-face colour programs, `func_80020F34` arms `.L80021DB4` and `.L80021FE0`
// (external/spyro-1 asm/renderers/r_moby.s). A triangle whose first prefix word has bit 2 set takes
// one of them instead of the three material-table colours the ordinary path reads; the control
// word's TOP byte chooses which. Quads never reach either: bit 2 on a quad selects the separate
// billboard program (`actor_billboard_face.h`).
//
//   top byte zero     `.L80021DB4`  one directional colour for all three vertices
//   top byte non-zero `.L80021FE0`  a per-vertex tint, and it writes the command byte itself
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
  Ready,         // `color` holds the 24-bit value retail gives each vertex
  NoEnvironment, // no magnitude table was supplied, so the directional program cannot run
  Degenerate,    // the face normal is zero, or its magnitude left the table's range
};

struct Result {
  Status status = Status::NoEnvironment;
  // Per vertex. The directional program gives all three the same value; the tint program gives
  // each its own, which is why this is three entries rather than one.
  std::array<std::uint32_t, 3> color{};
  // The tint program stores the packet's command byte as exactly 0x34, so a face that took it is
  // opaque even when its material word asks for semi-transparency. The directional program leaves
  // the command alone.
  bool opaqueCommand = false;
};

// `control` is the Moby's word at +0x4C, which both record builders copy to the draw record's
// +0x30 and the renderer parks in HI for the whole model. `material` is the three colours the
// ordinary path would have used; only the tint program reads them.
Result face_color(const std::array<ViewVertex, 3> &view,
                  const std::array<std::uint32_t, 3> &material,
                  std::uint32_t control,
                  const Environment &environment);

const char *status_name(Status status);

} // namespace spyro::face_light
