// The guest's own camera builder (FUN_8001c2f8), re-run so an in-between camera can interpolate.

// FUN_8001c2f8 is the only writer of the view matrix at 0x80067E98 and the projection matrix at
// 0x80067E84, which is the same matrix with row 1 scaled by 0x140 >> 9: the display's vertical
// scale. Only the input angles are interpolated; the packed matrices are scaled, shifted and
// truncated products of those angles, so they do not move linearly with them.

// The tables are the guest's and stay the guest's: `Builder` reads them through `Core` and never
// writes them. They are 320 signed entries wide, not 256, because the last interpolation reads the
// entry after the last one.

#pragma once

#include "core.h"
#include "guest_gte.h"

#include <array>
#include <cstdint>

namespace spyro::guest_camera {

// FUN_8001b61C and FUN_8001b664 both index `(angle & 0xFFF) >> 4` with the low nibble as the
// interpolation fraction, so 4096 is the period.
inline constexpr std::uint32_t kTurn = 0x1000;
// 0x140 >> 9 is 5/8, the display's vertical scale.
inline constexpr std::int32_t kVerticalScaleShift = 9;
inline constexpr std::int32_t kVerticalScale = 0x140;

// Three signed 16-bit angles read from 0x80067EC8, the low half of three consecutive words.
struct Angles {
  std::int32_t aroundX = 0; // the first angle halfword; Rx is the left factor
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

  // FUN_8001b61C / FUN_8001b664: the guest's own arithmetic, in the guest's order and with the same
  // truncating shift; a fraction rounded differently is a different angle and a different matrix.
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

// At f == 0 and f == 1 the result is the operand's angles bit for bit, so an in-between at either
// end of its interval is the guest's own camera.
Angles interpolate(const Angles &from, const Angles &to, double f);

} // namespace spyro::guest_camera