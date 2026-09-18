#include "core.h"
#include "face_light_program.h"
#include "gte_state.h"
#include "testutil.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace {

using spyro::face_light::Environment;
using spyro::face_light::LightColor;
using spyro::face_light::Status;
using spyro::face_light::ViewVertex;

// Data and control register numbers as the guest's mtc2/ctc2 name them.
constexpr std::uint32_t kIr0 = 8, kIr1 = 9, kIr2 = 10, kIr3 = 11, kRgbc = 6, kRgb2 = 22;
constexpr std::uint32_t kMac1 = 25, kMac2 = 26, kMac3 = 27, kLzcs = 30, kLzcr = 31;
constexpr std::uint32_t kR11R12 = 0, kR22R23 = 2, kR33 = 4;
constexpr std::uint32_t kRbk = 13, kGbk = 14, kBbk = 15;
constexpr std::uint32_t kLr1Lr2 = 16, kLr3Lg1 = 17, kLg2Lg3 = 18, kLb1Lb2 = 19, kLb3 = 20;

// The encoded words, taken from the instruction stream rather than from the mnemonics, because the
// shift and saturation flags they carry are the whole question this test settles.
constexpr std::uint32_t kOp = 0x4B70000Cu;
constexpr std::uint32_t kSqr = 0x4AA00428u;
constexpr std::uint32_t kGpf = 0x4B90003Du;
constexpr std::uint32_t kCc = 0x4B38041Cu;

std::int32_t sra(std::uint32_t value, unsigned shift) {
  return (std::int32_t)value >> shift;
}

// func_80020F34's `.L80021DB4`, instruction for instruction, against the real GTE: every value
// reaches a register through the same transfer path mtc2 and ctc2 use, and every operation is the
// encoded word the guest issues. A wrong truncation or a wrong shift flag in the pure program shows
// up here as a different colour instead of being papered over.
std::uint32_t reference_color(GteRegs &gte,
                              const std::array<ViewVertex, 3> &view,
                              std::uint32_t control,
                              const LightColor &light,
                              std::span<const std::int16_t> table) {
  GTE_BindState(&gte);
  auto run = [&](std::uint32_t insn) {
    GTE_ExecuteIsolated(&gte, insn);
  };
  auto packed = [](const ViewVertex &v) {
    return (std::uint32_t)(std::uint16_t)v.y | ((std::uint32_t)(std::uint16_t)v.z << 16);
  };

  const std::int32_t x0 = view[0].x, x1 = view[1].x, x2 = view[2].x;
  const std::uint32_t w0 = packed(view[0]), w1 = packed(view[1]), w2 = packed(view[2]);
  gte_write_data(kIr1, (std::uint32_t)(x1 - x0));
  gte_write_data(kIr2, (std::uint32_t)sra((w1 << 16) - (w0 << 16), 16));
  gte_write_data(kIr3, (std::uint32_t)(sra(w1, 16) - sra(w0, 16)));
  gte_write_ctrl(kR11R12, (std::uint32_t)(x2 - x0));
  gte_write_ctrl(kR22R23, (std::uint32_t)sra((w2 << 16) - (w0 << 16), 16));
  gte_write_ctrl(kR33, (std::uint32_t)(sra(w2, 16) - sra(w0, 16)));
  run(kOp);

  const std::int32_t n1 = sra(gte_read_data(kMac1), 4);
  const std::int32_t n2 = sra(gte_read_data(kMac3), 4);
  const std::int32_t n3 = sra(gte_read_data(kMac2), 4);
  gte_write_data(kIr1, (std::uint32_t)n1);
  gte_write_data(kIr2, (std::uint32_t)n2);
  gte_write_data(kIr3, (std::uint32_t)n3);
  run(kSqr);
  gte_write_ctrl(kRbk, (control >> 6) & 0xff0u);
  gte_write_ctrl(kGbk, control & 0xff0u);
  gte_write_ctrl(kBbk, (control << 6) & 0xff0u);
  gte_write_data(kIr1, (std::uint32_t)n1);
  gte_write_data(kIr2, (std::uint32_t)n2);
  gte_write_data(kIr3, (std::uint32_t)n3);
  const std::uint32_t sum = gte_read_data(kMac1) + gte_read_data(kMac2) + gte_read_data(kMac3);

  gte_write_data(kLzcs, sum);
  std::int32_t length = 0;
  if (sum != 0u) {
    const std::uint32_t even = gte_read_data(kLzcr) & ~1u;
    const unsigned exponent = (unsigned)((31 - (std::int32_t)even) >> 1);
    const std::int32_t excess = (std::int32_t)even - 24;
    const std::uint32_t mantissa =
        excess >= 0 ? sum << excess : (std::uint32_t)sra(sum, (unsigned)(-excess));
    const std::uint32_t offset = (mantissa - 0x40u) << 1;
    if (offset >= table.size() * sizeof(std::int16_t)) {
      return 0xffffffffu; // never a 24-bit colour, so the comparison fails loudly instead of
                          // reading past the table
    }
    length = (std::int32_t)((std::uint32_t)(table[offset / 2u] << exponent) >> 12);
  }
  const std::int32_t numerator = (std::int32_t)((control >> 18) << 14);
  const std::int32_t intensity = length == 0 ? (numerator >= 0 ? -1 : 1) : numerator / length;

  const std::uint32_t c0 = (std::uint32_t)(std::uint16_t)light.first;
  const std::uint32_t c1 = (std::uint32_t)(std::uint16_t)light.second;
  const std::uint32_t c2 = (std::uint32_t)(std::uint16_t)light.third;
  gte_write_ctrl(kLr1Lr2, (c1 << 16) + c0);
  gte_write_ctrl(kLr3Lg1, (c0 << 16) + c2);
  gte_write_ctrl(kLg2Lg3, (c2 << 16) + c1);
  gte_write_ctrl(kLb1Lb2, (c1 << 16) + c0);
  gte_write_ctrl(kLb3, c2);
  gte_write_data(kRgbc, 0x00ffffffu);
  gte_write_data(kIr0, (std::uint32_t)intensity);
  run(kGpf);
  gte_write_data(kIr1, (std::uint32_t)sra(gte_read_data(kMac1), 8));
  gte_write_data(kIr2, (std::uint32_t)sra(gte_read_data(kMac2), 8));
  gte_write_data(kIr3, (std::uint32_t)sra(gte_read_data(kMac3), 8));
  run(kCc);
  return gte_read_data(kRgb2);
}

struct Table {
  std::array<std::int16_t, spyro::face_light::kMagnitudeEntries> entries{};
  Table() {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      entries[i] = (std::int16_t)std::lround(std::sqrt((double)(i + 0x40)) * 362.0);
    }
  }
};

void test_matches_the_real_gte_over_a_spread_of_faces() {
  const Table table;
  const Environment environment{.light = {0x1000, 0x0800, 0x0400}, .magnitude = table.entries};
  GteRegs gte{};
  unsigned checked = 0;
  for (int spin = 1; spin <= 23; ++spin) {
    const std::array<ViewVertex, 3> view = {
        ViewVertex{
            (std::int16_t)(spin * 13), (std::int16_t)(-spin * 7), (std::int16_t)(600 + spin)},
        ViewVertex{
            (std::int16_t)(spin * 29), (std::int16_t)(spin * 11), (std::int16_t)(600 - spin)},
        ViewVertex{(std::int16_t)(-spin * 17), (std::int16_t)(spin * 23), (std::int16_t)(600)}};
    const std::uint32_t control = 0x0000A5A5u + (std::uint32_t)spin * 0x00040000u;
    const auto lit = spyro::face_light::face_color(view, control, environment);
    CHECK(lit.status == Status::Ready);
    CHECK_EQ(lit.color, reference_color(gte, view, control, environment.light, table.entries));
    ++checked;
  }
  CHECK_EQ(checked, 23u);
}

void test_the_check_can_fail() {
  // A discriminator that never reports a difference is not a check. Perturbing one vertex must
  // move the colour, or the comparison above is comparing two constants.
  const Table table;
  const Environment environment{.light = {0x1000, 0x0800, 0x0400}, .magnitude = table.entries};
  const std::array<ViewVertex, 3> base = {
      ViewVertex{0, 0, 600}, ViewVertex{400, 40, 590}, ViewVertex{-200, 500, 610}};
  std::array<ViewVertex, 3> moved = base;
  moved[2].z = 300;
  const auto first = spyro::face_light::face_color(base, 0x0014A5A5u, environment);
  const auto second = spyro::face_light::face_color(moved, 0x0014A5A5u, environment);
  CHECK(first.status == Status::Ready && second.status == Status::Ready);
  CHECK(first.color != second.color);
}

void test_the_additive_arm_is_reported_not_computed() {
  const Table table;
  const Environment environment{.light = {0x1000, 0x0800, 0x0400}, .magnitude = table.entries};
  const std::array<ViewVertex, 3> view = {
      ViewVertex{0, 0, 600}, ViewVertex{400, 40, 590}, ViewVertex{-200, 500, 610}};
  const auto lit = spyro::face_light::face_color(view, 0x02000000u, environment);
  CHECK(lit.status == Status::Additive);
  CHECK_EQ(lit.color, 0u);
}

void test_a_missing_table_refuses_rather_than_guessing() {
  const std::array<ViewVertex, 3> view = {
      ViewVertex{0, 0, 600}, ViewVertex{400, 40, 590}, ViewVertex{-200, 500, 610}};
  const auto lit = spyro::face_light::face_color(view, 0x0014A5A5u, Environment{});
  CHECK(lit.status == Status::NoEnvironment);
}

void test_a_collinear_face_has_no_normal_to_light() {
  const Table table;
  const Environment environment{.light = {0x1000, 0x0800, 0x0400}, .magnitude = table.entries};
  const std::array<ViewVertex, 3> view = {
      ViewVertex{0, 0, 0}, ViewVertex{100, 100, 100}, ViewVertex{200, 200, 200}};
  const auto lit = spyro::face_light::face_color(view, 0x0014A5A5u, environment);
  CHECK(lit.status == Status::Degenerate);
}

} // namespace

int main() {
  RUN(matches_the_real_gte_over_a_spread_of_faces);
  RUN(the_check_can_fail);
  RUN(the_additive_arm_is_reported_not_computed);
  RUN(a_missing_table_refuses_rather_than_guessing);
  RUN(a_collinear_face_has_no_normal_to_light);
  return pt_summary();
}
