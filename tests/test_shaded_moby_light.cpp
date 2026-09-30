#include "core.h"
#include "gte_state.h"
#include "shaded_moby_light.h"
#include "testutil.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using spyro::shaded_light::GteStage;
using spyro::shaded_light::Input;

// Data and control register numbers as the guest's mtc2/ctc2 name them.
constexpr std::uint32_t kIr0 = 8, kIr1 = 9, kIr2 = 10, kIr3 = 11, kRgbc = 6, kRgb2 = 22;
constexpr std::uint32_t kR11R12 = 0, kR13R21 = 1, kR22R23 = 2, kR31R32 = 3, kR33 = 4;
constexpr std::uint32_t kRbk = 13, kGbk = 14, kBbk = 15;
constexpr std::uint32_t kLr1Lr2 = 16, kLr3Lg1 = 17, kLg2Lg3 = 18, kLb1Lb2 = 19, kLb3 = 20;

// The encoded words, read out of the instruction stream (r_moby.s 0x8002315C, 0x80023188,
// 0x800231B0) rather than rebuilt from mnemonics, because the shift and saturation flags they carry
// are the question this test settles.
constexpr std::uint32_t kMvmva = 0x4A49E012u;
constexpr std::uint32_t kGpf = 0x4B90003Du;
constexpr std::uint32_t kCc = 0x4B38041Cu;

struct Random {
  std::uint32_t state;
  std::uint32_t next() {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
  }
};

std::uint32_t pack(std::int16_t low, std::int16_t high) {
  return (std::uint32_t)(std::uint16_t)low | ((std::uint32_t)(std::uint16_t)high << 16);
}

// 0x80023130-0x800231B0 against the real GTE, every value through the transfer path mtc2/ctc2 use.
GteStage reference(GteRegs &gte, const Input &input, const spyro::shaded_light::NormalIr &normal) {
  GTE_BindState(&gte);
  const auto &r = input.rotation;
  gte_write_ctrl(kR11R12, pack(r[0][0], r[0][1]));
  gte_write_ctrl(kR13R21, pack(r[0][2], r[1][0]));
  gte_write_ctrl(kR22R23, pack(r[1][1], r[1][2]));
  gte_write_ctrl(kR31R32, pack(r[2][0], r[2][1]));
  gte_write_ctrl(kR33, (std::uint32_t)(std::uint16_t)r[2][2]);
  gte_write_data(kIr1, (std::uint32_t)normal[0]);
  gte_write_data(kIr2, (std::uint32_t)normal[1]);
  gte_write_data(kIr3, (std::uint32_t)normal[2]);
  GTE_ExecuteIsolated(&gte, kMvmva);

  gte_write_data(kIr0, (input.entry >> 23) & 0x1eu);
  GTE_ExecuteIsolated(&gte, kGpf);

  const auto &c = input.colourMatrix;
  gte_write_data(kRgbc, 0x00ffffffu);
  gte_write_ctrl(kRbk, (input.entry << 4) & 0xff0u);
  gte_write_ctrl(kGbk, (input.entry >> 4) & 0xff0u);
  gte_write_ctrl(kBbk, (input.entry >> 12) & 0xff0u);
  gte_write_ctrl(kLr1Lr2, pack(c[0][0], c[0][1]));
  gte_write_ctrl(kLr3Lg1, pack(c[0][2], c[1][0]));
  gte_write_ctrl(kLg2Lg3, pack(c[1][1], c[1][2]));
  gte_write_ctrl(kLb1Lb2, pack(c[2][0], c[2][1]));
  gte_write_ctrl(kLb3, (std::uint32_t)(std::uint16_t)c[2][2]);
  GTE_ExecuteIsolated(&gte, kCc);
  return {.rgb = gte_read_data(kRgb2),
          .ir = {(std::int32_t)gte_read_data(kIr1),
                 (std::int32_t)gte_read_data(kIr2),
                 (std::int32_t)gte_read_data(kIr3)}};
}

std::int16_t signedHalf(Random &random, std::uint32_t range) {
  return (std::int16_t)((std::int32_t)(random.next() % (2u * range + 1u)) - (std::int32_t)range);
}

Input randomInput(Random &random) {
  Input input{};
  for (auto &row : input.rotation) {
    for (auto &value : row) {
      value = signedHalf(random, 0x1000u);
    }
  }
  // The guest splays three halfwords across all three rows (0x80022AC8-0x80022B04).
  const std::array<std::int16_t, 3> light = {
      signedHalf(random, 0x1000u), signedHalf(random, 0x1000u), signedHalf(random, 0x1000u)};
  for (auto &row : input.colourMatrix) {
    row = light;
  }
  input.entry = random.next() ^ (random.next() << 16);
  return input;
}

// The GTE half, and the IR values the highlight step reads, against the hardware reference over a
// spread of matrices, light colours, lighting words and normals, including saturating ones. A wrong
// shift, clamp or lane order shows here as a different colour. Both normal encodings go through it.
void test_the_gte_half_matches_the_real_gte() {
  GteRegs gte{};
  Random random{0x5eed5eedu};
  std::uint32_t compared = 0;
  std::uint32_t saturatedHigh = 0;
  std::uint32_t saturatedLow = 0;
  for (int i = 0; i < 4000; ++i) {
    const Input input = randomInput(random);
    const auto normal =
        (i % 2 == 0) ? spyro::shaded_light::vertexNormal({(std::int8_t)random.next(),
                                                          (std::int8_t)random.next(),
                                                          (std::int8_t)random.next()})
                     : spyro::shaded_light::faceNormal(random.next() ^ (random.next() << 16));
    const GteStage want = reference(gte, input, normal);
    const GteStage got = spyro::shaded_light::gteStage(input, normal);
    CHECK_EQ(got.rgb, want.rgb);
    CHECK_EQ(got.ir[0], want.ir[0]);
    CHECK_EQ(got.ir[1], want.ir[1]);
    CHECK_EQ(got.ir[2], want.ir[2]);
    ++compared;
    const bool high = (want.rgb & 0xffu) == 0xffu || ((want.rgb >> 8) & 0xffu) == 0xffu;
    const bool low = want.ir[0] == 0 || want.ir[1] == 0 || want.ir[2] == 0;
    saturatedHigh += high ? 1u : 0u;
    saturatedLow += low ? 1u : 0u;
  }
  CHECK_EQ(compared, 4000u);
  // The spread must actually reach both colour clamps or it proved nothing about them.
  CHECK(saturatedHigh > 0u);
  CHECK(saturatedLow > 0u);
}

// The two encodings order the same signed bytes differently, and getting that wrong rotates the
// wrong axis. Bytes 1, 2, 3 of a word are IR1, IR2, IR3 read from the word's high end; in memory
// order byte 0 is IR3.
void test_the_two_normal_encodings_order_their_bytes_as_retail_loads_them() {
  using spyro::shaded_light::faceNormal;
  using spyro::shaded_light::vertexNormal;
  const auto vertex = vertexNormal({1, -2, 3});
  CHECK_EQ(vertex[0], -2);
  CHECK_EQ(vertex[1], 3);
  CHECK_EQ(vertex[2], 1);
  // 0x80 in the top byte is -128 (sign-extended), 0x7F in bits 16..23 is 127, 0xFE in bits 8..15
  // -2.
  const auto face = faceNormal(0x807ffe55u);
  CHECK_EQ(face[0], 127);
  CHECK_EQ(face[1], -2);
  CHECK_EQ(face[2], -128);
}

GteStage stageOf(std::uint32_t rgb, std::array<std::int32_t, 3> ir) {
  return {.rgb = rgb, .ir = ir};
}

// A colour below the entry's own value is replaced by it, and the comparison is over the PACKED
// word (0x800231C4 `sub $a1,$a0,$s1; bgez`): 0x0000FF is below 0x000100 even though its red
// channel is the larger, which a per-channel maximum would get the other way round.
void test_a_vertex_colour_below_the_entry_is_replaced_as_a_packed_word() {
  using spyro::shaded_light::vertexColour;
  CHECK_EQ(vertexColour(0x00000100u, stageOf(0x0000ffu, {0, 0, 0})), 0x000100u);
  CHECK_EQ(vertexColour(0x00000100u, stageOf(0x000100u, {0, 0, 0})), 0x000100u);
  CHECK_EQ(vertexColour(0x00000100u, stageOf(0x000101u, {0, 0, 0})), 0x000101u);
  // The variant-1 face arm has no floor: the same stage keeps its own colour there.
  CHECK_EQ(spyro::shaded_light::highlight(0x00000100u, stageOf(0x0000ffu, {0, 0, 0})), 0x0000ffu);
}

void test_the_highlight_needs_a_nonzero_nibble_and_an_excess() {
  using spyro::shaded_light::highlight;
  // Top nibble 5: threshold 5 << 7 = 640, and RBK = 0x10 << 4 = 256, so IR1 = 1500 leaves an excess
  // of 604, doubled to 1208. Each channel becomes (IR + 1208) >> 4.
  constexpr std::uint32_t kEntry = 0x50000010u;
  CHECK_EQ(highlight(kEntry, stageOf(0x3f3f3fu, {1500, 1000, 800})), 0x7d8aa9u);
  // Mixed: excess 704, doubled 1408, so (1600, 100, 2000) become (3008, 1508, 3408) >> 4.
  CHECK_EQ(highlight(kEntry, stageOf(0x3f3f3fu, {1600, 100, 2000})), 0xd55ebcu);
  // Saturation: a channel at or past 0x1000 is 0xFF.
  CHECK_EQ(highlight(kEntry, stageOf(0x3f3f3fu, {3000, 2000, 10})), 0xffffffu);
  // Negative: an IR1 that does not clear the threshold keeps the CC colour.
  CHECK_EQ(highlight(kEntry, stageOf(0x3f3f3fu, {896, 1000, 800})), 0x3f3f3fu);
  // Negative: the same lit brightness with a zero nibble keeps the CC colour.
  CHECK_EQ(highlight(0x00000010u, stageOf(0x3f3f3fu, {3000, 2000, 10})), 0x3f3f3fu);
}

void test_the_composed_entry_points_are_the_stage_plus_their_selection() {
  Random random{0x1234abcdu};
  const Input input = randomInput(random);
  std::vector<std::array<std::int8_t, 3>> normals;
  normals.reserve(9);
  for (int i = 0; i < 9; ++i) {
    normals.push_back(
        {(std::int8_t)random.next(), (std::int8_t)random.next(), (std::int8_t)random.next()});
  }
  const auto lit = spyro::shaded_light::vertexColours(input, normals);
  CHECK_EQ(lit.size(), normals.size());
  for (std::size_t i = 0; i < normals.size(); ++i) {
    CHECK_EQ(
        lit[i],
        spyro::shaded_light::vertexColour(
            input.entry,
            spyro::shaded_light::gteStage(input, spyro::shaded_light::vertexNormal(normals[i]))));
  }
  CHECK(spyro::shaded_light::vertexColours(input, {}).empty());
  const std::uint32_t word = random.next() ^ (random.next() << 16);
  CHECK_EQ(spyro::shaded_light::faceColour(input, word),
           spyro::shaded_light::highlight(
               input.entry,
               spyro::shaded_light::gteStage(input, spyro::shaded_light::faceNormal(word))));
}

} // namespace

int main() {
  RUN(the_gte_half_matches_the_real_gte);
  RUN(the_two_normal_encodings_order_their_bytes_as_retail_loads_them);
  RUN(a_vertex_colour_below_the_entry_is_replaced_as_a_packed_word);
  RUN(the_highlight_needs_a_nonzero_nibble_and_an_excess);
  RUN(the_composed_entry_points_are_the_stage_plus_their_selection);
  return pt_summary();
}
