// The guest's own camera builder, re-run over the guest's own tables.

#include "guest_camera_builder.h"

#include "gte_registers.h"

#include <algorithm>
#include <cmath>

namespace spyro::guest_camera {
namespace {

// The GTE's rotation elements are 12-bit fractions, so MVMVA's shift is 12.
constexpr std::int32_t kShift = 12;
// FUN_80059d6C saturates its products because the GTE wrote them.
constexpr std::int32_t kMin = -0x8000;
constexpr std::int32_t kMax = 0x7FFF;

struct Element3 {
  std::int32_t row[3][3]{};
};

// One `MVMVA sf, IR * V0` column product, in int64 so the products cannot overflow before the
// shift.
Element3 product(const Element3 &ir, const Element3 &matrix) {
  Element3 out{};
  for (std::int32_t column = 0; column < 3; ++column) {
    for (std::int32_t row = 0; row < 3; ++row) {
      const std::int64_t sum = static_cast<std::int64_t>(ir.row[row][0]) * matrix.row[0][column] +
                               static_cast<std::int64_t>(ir.row[row][1]) * matrix.row[1][column] +
                               static_cast<std::int64_t>(ir.row[row][2]) * matrix.row[2][column];
      out.row[row][column] = std::clamp<std::int64_t>(sum >> kShift, kMin, kMax);
    }
  }
  return out;
}

// The five control words, in the packing both the drawer and the classification pass read.
std::array<std::uint32_t, psx::gte::kRotationWords> controlWords(const Element3 &matrix) {
  const auto element = [](std::int32_t value) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
  };
  return {element(matrix.row[0][0]) | (element(matrix.row[0][1]) << 16),
          element(matrix.row[0][2]) | (element(matrix.row[1][0]) << 16),
          element(matrix.row[1][1]) | (element(matrix.row[1][2]) << 16),
          element(matrix.row[2][0]) | (element(matrix.row[2][1]) << 16),
          element(matrix.row[2][2])};
}

// The order 0x8001c2f8 lays the rotations out: Ry into halfwords 0..9, Rz into 10..19, Rx
// into 20..29.

// Ry, on rows 1 and 2 with x fixed.
Element3 aroundY(std::int32_t c, std::int32_t s) {
  Element3 out{};
  out.row[0][0] = 0x1000;
  out.row[1][1] = c;
  out.row[1][2] = -s;
  out.row[2][1] = s;
  out.row[2][2] = c;
  return out;
}

// Rz, on rows 0 and 2 with y fixed.
Element3 aroundZ(std::int32_t c, std::int32_t s) {
  Element3 out{};
  out.row[0][0] = c;
  out.row[0][2] = s;
  out.row[1][1] = 0x1000;
  out.row[2][0] = -s;
  out.row[2][2] = c;
  return out;
}

// Rx, on rows 0 and 1 with z fixed.
Element3 aroundX(std::int32_t c, std::int32_t s) {
  Element3 out{};
  out.row[0][0] = c;
  out.row[0][1] = s;
  out.row[1][0] = -s;
  out.row[1][1] = c;
  out.row[2][2] = 0x1000;
  return out;
}

std::int32_t wrapOne(std::int32_t from, std::int32_t to, double f) {
  const auto a = static_cast<std::int64_t>(from) & (kTurn - 1);
  // Take the short way round: an angle pair 4095 and 1 is one step apart in the space the tables
  // cover, and the long way would turn the camera most of a full circle inside one field.
  const std::int64_t turn = static_cast<std::int64_t>(kTurn);
  std::int64_t step = ((static_cast<std::int64_t>(to) - a) % turn + turn) % turn;
  if (step > turn / 2) {
    step -= turn;
  }
  return static_cast<std::int32_t>(
      (a + static_cast<std::int64_t>(std::llround(static_cast<double>(step) * f))) & (kTurn - 1));
}

} // namespace

std::int32_t Builder::entry(std::uint32_t table, std::uint32_t index) const {
  const std::uint32_t half = core_.mem_r32(table + 2 * index) & 0xFFFFu;
  return half >= 0x8000u ? static_cast<std::int32_t>(half) - 0x10000
                         : static_cast<std::int32_t>(half);
}

std::int32_t Builder::lookup(std::uint32_t angle, std::uint32_t table) const {
  const std::uint32_t index = (angle & 0xFFFu) >> 4;
  const std::int32_t value = entry(table, index);
  const std::uint32_t fraction = angle & 0xFu;
  if (fraction == 0) {
    return value;
  }
  // The fraction scales the step to the next table entry and the whole thing shifts down by four;
  // truncating toward negative infinity is what the guest's `>>` does.
  return value + ((static_cast<std::int32_t>(fraction) * (entry(table, index + 1) - value)) >> 4);
}

Matrices Builder::build(const Angles &angles) const {
  const auto y = static_cast<std::uint32_t>(angles.aroundY);
  const auto z = static_cast<std::uint32_t>(angles.aroundZ);
  const auto x = static_cast<std::uint32_t>(angles.aroundX);

  // FUN_80059D6C(a, b) is a * b: first Ry * Rz, then Rx * that.
  Element3 view =
      product(aroundX(cos(x), sin(x)), product(aroundY(cos(y), sin(y)), aroundZ(cos(z), sin(z))));

  Matrices out{};
  out.view = controlWords(view);
  // Row 1 (R21, R22, R23) by 0x140 >> 9 for the projection matrix at 0x80067E84.
  for (std::int32_t column = 0; column < 3; ++column) {
    view.row[1][column] = (view.row[1][column] * kVerticalScale) >> kVerticalScaleShift;
  }
  out.projection = controlWords(view);
  return out;
}

Angles interpolate(const Angles &from, const Angles &to, double f) {
  Angles out{};
  out.aroundX = wrapOne(from.aroundX, to.aroundX, f);
  out.aroundY = wrapOne(from.aroundY, to.aroundY, f);
  out.aroundZ = wrapOne(from.aroundZ, to.aroundZ, f);
  return out;
}

} // namespace spyro::guest_camera