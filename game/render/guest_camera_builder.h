// guest_camera_builder.h — the guest's OWN camera builder, recovered and re-run for an in-between.
//
// WHY THIS EXISTS. The five words the terrain drawer loads into the GTE's rotation registers are
// DERIVED. Recovered 2026-10-03 from SCUS_944.25, whose per-field body is 0x800156FC:
//
//   FUN_8001c2f8(&0x80067EC8, &0x80067E98, &0x80067E84);   // the FIRST thing it does
//   FUN_8001bdb0(&0x80067EB8, &0x80067EAC);                 // position: last field's <- current
//
// 0x80067EC8 is the CAMERA STATE: three signed 16-bit ANGLES, and nothing else. FUN_8001c2f8 reads
// exactly those three (its `param_1[0..2]`, read as `short`), builds a rotation from them with the
// image's own trig tables, multiplies the three rotations together with the GTE (FUN_80059D6C,
// three `MVMVA sf, IR * V0` column products), and writes two five-word matrices:
//
//   0x80067E98   the VIEW matrix — the classification pass's, unscaled;
//   0x80067E84   the PROJECTION matrix the drawer uses — the same, with row 1 (R21, R22, R23)
//                multiplied by 0x140 >> 9, which is 5/8 and is the display's vertical scale.
//
// MEASURED, both claims, on SCUS_944.25 in Glimmer:
//   - otattr last-writer provenance on 0x80067E84..0x80067EA8 names 0x800156FC for both matrices,
//     and that function's first two calls are the two above, so this builder is the ONLY writer of
//     the words an interpolated camera used to lerp;
//   - the matrices this file builds are bit-identical to the guest's own, for every camera held on
//     the route, at every angle: `build(capturedAngles) == the guest's five words` (see
//     docs/re-frontier.md `guest.camera-builder`).
//
// SO THE IN-BETWEEN INTERPOLATES THE INPUTS. Three angles, each in the 4096-step turn the tables
// cover, taken the SHORT way round, plus the three position words — never the packed output, whose
// elements are scaled, shifted and truncated products of those angles and therefore do not move
// linearly with them. A real field does not come here at all: it reads the guest's own words, so
// retail's field stays byte for byte retail's.
//
// The two tables are the guest's and stay the guest's: `Builder` reads them through `Core`, never
// copies them, and never writes them. They are 320 signed entries wide, not 256 — MEASURED on
// SCUS_944.25, whose sine table runs 0x80061BD8..0x80061E97 and whose 256th and 257th entries are
// the turn's own zero and its first step — because the last interpolation reads the entry after the
// last one.

#pragma once

#include "core.h"
#include "guest_gte.h"

#include <array>
#include <cstdint>

namespace spyro::guest_camera {

// One turn of the guest's angle space. FUN_8001b61C and FUN_8001b664 both index
// `(angle & 0xFFF) >> 4` with the low nibble as the interpolation fraction, so 4096 IS the period
// and an angle that runs 4095 -> 1 is one step, not a turn backwards.
inline constexpr std::uint32_t kTurn = 0x1000;
// 0x140 >> 9, the row scale the builder applies before writing 0x80067E84.
inline constexpr std::int32_t kVerticalScaleShift = 9;
inline constexpr std::int32_t kVerticalScale = 0x140;

// The three angles, as the guest holds them: one signed 16-bit value per rotation, read from three
// consecutive words at 0x80067EC8 (the low half of the first two, the low half of the third).
struct Angles {
  std::int32_t aroundX = 0; // the Rx the builder makes first
  std::int32_t aroundY = 0;
  std::int32_t aroundZ = 0;
};

// Both matrices the builder writes, in the GTE's own control-register packing.
struct Matrices {
  std::array<std::uint32_t, guest_gte::kRotationWords> projection{};
  std::array<std::uint32_t, guest_gte::kRotationWords> view{};
};

// FUN_8001c2f8. `sinTable` and `cosTable` are the image's own (SCUS_944.25: 0x80061BD8 sine,
// 0x80061C58 cosine), read one entry at a time and never written.
class Builder {
public:
  Builder(Core &core, std::uint32_t sinTable, std::uint32_t cosTable)
      : core_(core), sinTable_(sinTable), cosTable_(cosTable) {}

  // FUN_8001b61C / FUN_8001b664: a 256-entry quarter-period table read with the low nibble as the
  // interpolation fraction. The interpolation is the guest's own arithmetic, in the guest's own
  // order and with the same truncating shift, because a fraction rounded differently is a different
  // angle and therefore a different matrix.
  [[nodiscard]] std::int32_t sin(std::uint32_t angle) const {
    return lookup(angle, sinTable_);
  }
  [[nodiscard]] std::int32_t cos(std::uint32_t angle) const {
    return lookup(angle, cosTable_);
  }

  [[nodiscard]] Matrices build(const Angles &angles) const;

private:
  [[nodiscard]] std::int32_t entry(std::uint32_t table, std::uint32_t index) const;
  [[nodiscard]] std::int32_t lookup(std::uint32_t angle, std::uint32_t table) const;

  Core &core_;
  std::uint32_t sinTable_;
  std::uint32_t cosTable_;
};

// THE ANGLES BETWEEN TWO OF THE GUEST'S OWN, the short way round each turn. `f` is the in-between's
// position between its two real endpoints, and the endpoints are the endpoints: at f == 0 and
// f == 1 the result is the operand's angles bit for bit, which is what makes an in-between at
// either end of its interval the guest's own camera.
Angles interpolate(const Angles &from, const Angles &to, double f);

} // namespace spyro::guest_camera