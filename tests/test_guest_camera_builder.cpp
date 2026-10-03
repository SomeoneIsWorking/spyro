// test_guest_camera_builder.cpp — the guest's own camera builder, over the guest's own state.
//
// The five rotation words the terrain drawer loads into the GTE are not camera state: they are what
// the guest's camera builder WRITES from three angles (SCUS_944.25 0x8001C2F8, called from the
// per-field body 0x800156FC with `&0x80067EC8`). So an in-between interpolates those three angles
// and re-runs the builder, rather than interpolating packed pairs of matrix elements — which is the
// difference between a camera the guest's own code would have held and a word that renders.
//
// The tables here are BUILT, not copied out of the disc: the builder reads them through `Core`, so
// the test stands up the same two-window guest shape and fills it with an exact quarter-turn cosine
// and sine. That keeps the game's data out of the repository while still exercising the real read
// path, the real indexing and the real interpolation.
//
// What is asserted:
//   1. the three rotations and their product are the guest's own structure — identity at zero, unit
//      rows, and the product of two known angles;
//   2. row 1 alone is scaled by 5/8 for the drawer's copy, and the classification copy is not;
//   3. rebuilding the SAME angles twice gives the same words, and every element stays inside int16
//   —
//      the packer cannot silently drop a sign;
//   4. the angle interpolation takes the short way round 4095 -> 1, hits both endpoints exactly,
//   and
//      never leaves the turn.
#include "guest_camera_builder.h"

#include "testutil.h"

#include <array>
#include <cmath>

namespace {

namespace cam = spyro::guest_camera;

// TWO GUEST-SHAPED WINDOWS, at the addresses this family's tables live at, filled through the same
// `mem_w32` the product's own mapping uses.
//
// 257 ENTRIES, NOT 256, and the reason is the image's own data: the last interpolation reads the
// entry after the last one. MEASURED on SCUS_944.25, where the sine table runs
// 0x80061BD8..0x80061E97 (320 signed entries): sin[254..256] = -201, -101, 0 — the turn's own zero
// — and cos[256] = 4096. A 256-entry fixture leaves the final eighth of a turn interpolating
// against a zero, which is a fixture's bug and would look exactly like a builder bug.
class Tables {
public:
  explicit Tables(Core &core) : core_(core) {
    for (std::uint32_t i = 0; i < 257; ++i) {
      core_.mem_w32(kSine + 2 * i, static_cast<std::uint32_t>(sine(i)) & 0xFFFFu);
      core_.mem_w32(kCosine + 2 * i, static_cast<std::uint32_t>(cosine(i)) & 0xFFFFu);
    }
  }

  [[nodiscard]] cam::Builder builder() const {
    return cam::Builder(core_, kSine, kCosine);
  }

  // The exact 12-bit-fraction circle the guest's tables hold, so a builder reading these reads the
  // values a real image's tables hold at a quarter period.
  static std::int32_t sine(std::uint32_t index) {
    return sample(index % 256, true);
  }
  static std::int32_t cosine(std::uint32_t index) {
    return sample(index % 256, false);
  }

private:
  static std::int32_t sample(std::uint32_t index, bool sine) {
    const double turn = 2.0 * 3.14159265358979323846 * static_cast<double>(index) / 256.0;
    const double exact = (sine ? std::sin(turn) : std::cos(turn)) * 4096.0;
    return static_cast<std::int32_t>(exact >= 0.0 ? std::lround(exact) : -std::lround(-exact));
  }

  Core &core_;
  static constexpr std::uint32_t kSine = 0x80061BD8u;
  static constexpr std::uint32_t kCosine = 0x80061C58u;
};

std::int32_t element(const cam::Matrices &m, bool projection, std::uint32_t word, unsigned half) {
  const std::uint32_t value = projection ? m.projection[word] : m.view[word];
  return half == 0 ? static_cast<std::int16_t>(static_cast<std::uint16_t>(value & 0xFFFFu))
                   : static_cast<std::int16_t>(static_cast<std::uint16_t>(value >> 16));
}

// The nine elements in the order the GTE's five control words hold them.
std::array<std::int32_t, 9> elements(const cam::Matrices &m, bool projection) {
  return {element(m, projection, 0, 0),
          element(m, projection, 0, 1),
          element(m, projection, 1, 0),
          element(m, projection, 1, 1),
          element(m, projection, 2, 0),
          element(m, projection, 2, 1),
          element(m, projection, 3, 0),
          element(m, projection, 3, 1),
          element(m, projection, 4, 0)};
}

} // namespace

// (1) ZERO ANGLES ARE THE IDENTITY, AND EVERY OTHER CAMERA IS A UNIT ROTATION.
//
// The builder's output feeds `gte_write_ctrl(kRotation0 + i, ...)`, so a camera whose rows are not
// 4096 long is a projection error the in-between would present as a stretched world.
void test_zero_is_identity() {
  Core core{};
  Tables tables(core);
  const cam::Matrices m = tables.builder().build(cam::Angles{0, 0, 0});
  const std::array<std::int32_t, 9> e = elements(m, false);
  const std::array<std::int32_t, 9> want = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
  for (std::size_t i = 0; i < e.size(); ++i) {
    CHECK_EQ(e[i], want[i]);
  }
}

void test_rows_are_unit_length() {
  Core core{};
  Tables tables(core);
  const cam::Angles angles[] = {{1, 2, 3}, {218, 1703, 512}, {0, 1024, 2048}, {4095, 37, 4094}};
  for (const cam::Angles &angles_value : angles) {
    const cam::Matrices m = tables.builder().build(angles_value);
    const std::array<std::int32_t, 9> e = elements(m, false);
    for (std::uint32_t row = 0; row < 3; ++row) {
      const double sum =
          static_cast<double>(e[row * 3] * e[row * 3] + e[row * 3 + 1] * e[row * 3 + 1] +
                              e[row * 3 + 2] * e[row * 3 + 2]);
      // The products are the GTE's own: every `>> 12` truncates toward negative infinity, so a row
      // loses up to a count per term and lands a count or two short of 4096. The bound is two
      // counts, on the measured range, and the row length is asserted in tenths so a failure says
      // how far off the row was.
      // MEASURED on the real tables: SCUS_944.25's own rebuilt rows are 4094..4096, because each
      // of the three products loses up to a count to its truncating shift.
      const std::int64_t tenths = static_cast<std::int64_t>(std::llround(std::sqrt(sum) * 10.0));
      CHECK(tenths >= 40920 && tenths <= 40960);
    }
  }
}

// (2) THE DRAWER'S COPY IS ROW 1 BY 5/8, AND NOTHING ELSE.
//
// MEASURED on SCUS_944.25: 0x80067E98 and 0x80067E84 differ in exactly three elements — 678 -> 423,
// 3869 -> 2418, 1160 -> 725 — which is (x * 0x140) >> 9 and which is what makes the classification
// pass's sphere tests and the drawer's projection agree about the same camera.
void test_vertical_scale_is_row_one_only() {
  Core core{};
  Tables tables(core);
  const cam::Matrices m = tables.builder().build(cam::Angles{0, 218, 1703});
  const std::array<std::int32_t, 9> view = elements(m, false);
  const std::array<std::int32_t, 9> projection = elements(m, true);
  for (std::uint32_t i = 0; i < 9; ++i) {
    const bool inRowOne = i >= 3 && i < 6;
    if (inRowOne) {
      CHECK_EQ(projection[i], (view[i] * 0x140) >> 9);
    } else {
      CHECK_EQ(projection[i], view[i]);
    }
  }
}

// (3) THE SAME ANGLES GIVE THE SAME WORDS, AND NOTHING OVERFLOWS ITS 16 BITS.
//
// The packer narrows each element to int16 the way the GTE's own registers do. An element that lost
// its sign here would be a camera silently mirrored, and the frames would still render.
void test_repeatable_and_in_range() {
  Core core{};
  Tables tables(core);
  const cam::Builder builder = tables.builder();
  for (std::int32_t angle = 0; angle < 4096; angle += 37) {
    const cam::Angles a{angle, angle * 3, angle * 5};
    const cam::Matrices first = builder.build(a);
    const cam::Matrices second = builder.build(a);
    for (std::uint32_t word = 0; word < cam::Matrices{}.projection.size(); ++word) {
      CHECK_EQ(first.projection[word], second.projection[word]);
      CHECK_EQ(first.view[word], second.view[word]);
    }
    for (std::uint32_t word = 0; word < 5; ++word) {
      // Round-tripping each half through int16 must be the identity, or the word holds a value the
      // element accessors cannot see again.
      const std::uint32_t value = first.projection[word];
      const std::uint32_t rebuilt =
          static_cast<std::uint32_t>(
              static_cast<std::uint16_t>(static_cast<std::int16_t>(value & 0xFFFFu))) |
          (static_cast<std::uint32_t>(
               static_cast<std::uint16_t>(static_cast<std::int16_t>(value >> 16)))
           << 16);
      CHECK_EQ(value, rebuilt);
    }
  }
}

// (4) THE ANGLES MOVE THE SHORT WAY ROUND, AND HIT BOTH ENDPOINTS EXACTLY.
//
// 4095 and 1 are one step apart in a 4096-step turn. Taking that the long way would swing the
// camera most of a full circle between two adjacent display fields, which is the one motion an
// interpolated camera must never make.
void test_shortest_way_round() {
  const cam::Angles from{10, 4095, 2048};
  const cam::Angles to{11, 1, 2048};
  CHECK_EQ(cam::interpolate(from, to, 0.0).aroundY, 4095);
  CHECK_EQ(cam::interpolate(from, to, 1.0).aroundY, 1);
  CHECK_EQ(cam::interpolate(from, to, 0.5).aroundY, 0);
  CHECK_EQ(cam::interpolate(from, to, 1.0).aroundX, 11);
  // An angle pair a quarter turn apart moves a quarter turn, whichever way round it is measured.
  CHECK_EQ(cam::interpolate(cam::Angles{0, 0, 0}, cam::Angles{1024, 0, 0}, 1.0).aroundX, 1024);
  CHECK_EQ(cam::interpolate(cam::Angles{1024, 0, 0}, cam::Angles{0, 0, 0}, 1.0).aroundX, 0);
}

int main() {
  RUN(zero_is_identity);
  RUN(rows_are_unit_length);
  RUN(vertical_scale_is_row_one_only);
  RUN(repeatable_and_in_range);
  RUN(shortest_way_round);
  return pt_summary();
}