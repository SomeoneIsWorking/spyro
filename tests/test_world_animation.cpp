// test_world_animation.cpp — the phase-1 animation decode, channel by channel and form by form.
//
// The live corpus (scratch/raw/stage0_artisans_refusal.bin, compared byte-for-byte against the
// retained body by PSXPORT_WORLD_ANIMATION_ORACLE_SNAPSHOT) exercises the DIRECT form only: the
// Artisans frame that blocked issue 0089 has two straight-copy channels and no blended one. So the
// interpolated forms are covered here instead, and the fixtures state that split rather than
// letting a green run imply coverage it does not have.
#include "world_animation.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

using spyro::world_animation::Plan;
using spyro::world_animation::Vector3;
using spyro::world_chunk_codec::RamView;

namespace {

int g_checks = 0;
int g_failed = 0;

void check(bool value, const char *what) {
  g_checks++;
  if (!value) {
    g_failed++;
    std::printf("    FAIL: %s\n", what);
  }
}

constexpr uint32_t kAnimationSets = 0x78560u + 20u;
constexpr uint32_t kSector = 0x91000u;
constexpr uint32_t kSet = 0x93000u;
constexpr uint32_t kAnimation = 0x94000u;
constexpr uint32_t kPayload = 0x400u; // relative to kAnimation, as the guest's +8 field is

void w8(std::vector<uint8_t> &ram, uint32_t address, uint8_t value) {
  ram[address] = value;
}

void w16(std::vector<uint8_t> &ram, uint32_t address, uint16_t value) {
  w8(ram, address, (uint8_t)value);
  w8(ram, address + 1u, (uint8_t)(value >> 8));
}

void w32(std::vector<uint8_t> &ram, uint32_t address, uint32_t value) {
  for (uint32_t i = 0; i < 4u; ++i) {
    w8(ram, address + i, (uint8_t)(value >> (i * 8u)));
  }
}

uint32_t r32(const std::vector<uint8_t> &ram, uint32_t address) {
  uint32_t out = 0;
  for (uint32_t i = 0; i < 4u; ++i) {
    out |= (uint32_t)ram[address + i] << (i * 8u);
  }
  return out;
}

// One animation, two keyframes, `size` bytes each. `factor` 0 selects the straight-copy form.
void buildAnimation(std::vector<uint8_t> &ram,
                    uint32_t channel,
                    uint8_t index,
                    uint16_t size,
                    uint8_t factor,
                    const std::vector<uint32_t> &keyA,
                    const std::vector<uint32_t> &keyB) {
  const uint32_t set = kSet + channel * 0x100u;
  const uint32_t animation = kAnimation + channel * 0x2000u;
  w32(ram, kAnimationSets + channel * 8u, set);
  w32(ram, set + (uint32_t)index * 4u, animation);
  w8(ram, animation + 2u, 0u); // keyframe cursor
  w16(ram, animation + 6u, size);
  w32(ram, animation + 8u, kPayload);
  w8(ram, animation + 12u + 4u, factor);
  w8(ram, animation + 12u + 5u, 0u); // keyframe A slot
  w8(ram, animation + 12u + 6u, 1u); // keyframe B slot
  for (size_t i = 0; i < keyA.size(); ++i) {
    w32(ram, animation + kPayload + (uint32_t)i * 4u, keyA[i]);
  }
  for (size_t i = 0; i < keyB.size(); ++i) {
    w32(ram, animation + kPayload + size + (uint32_t)i * 4u, keyB[i]);
  }
}

// `active` as the renderer forms it: the sector's four stamp bytes ORed with the quality mask. A
// channel is live when its byte is below 0x80; 0xFF is the retired sentinel.
uint32_t activeFor(uint32_t channel, uint8_t index) {
  return 0xffffffffu & ~(0xffu << (channel * 8u)) | ((uint32_t)index << (channel * 8u));
}

uint32_t packVertex(uint32_t x, uint32_t y, uint32_t z) {
  return (x << 21) | (y << 10) | z;
}

void expectRefusal(std::vector<uint8_t> &ram, uint32_t active, const char *expected) {
  Plan plan{};
  const char *why = "none";
  const bool ok = spyro::world_animation::appendSector(RamView(ram), kSector, active, plan, why);
  check(!ok, expected);
  check(std::string_view(why) == std::string_view(expected), expected);
}

void test_channel0_direct_copies_vertices() {
  std::printf("test channel0_direct_copies_vertices\n");
  std::vector<uint8_t> ram(0x200000u);
  const std::vector<uint32_t> source{0x11111111u, 0x22222222u, 0x33333333u};
  buildAnimation(ram, 0u, 3u, 12u, 0u, source, {});
  Plan plan{};
  const char *why = "none";
  check(spyro::world_animation::appendSector(RamView(ram), kSector, activeFor(0u, 3u), plan, why),
        "decodes");
  // Three payload words plus the channel's retirement stamp.
  check(plan.writes.size() == 4u, "write count");
  check(plan.channels == 1u && plan.direct == 1u && plan.blended == 0u, "counters");
  for (uint32_t i = 0; i < 3u; ++i) {
    check(plan.writes[i].address == kSector + 28u + i * 4u, "vertex destination");
    check(plan.writes[i].value == source[i], "vertex value");
  }
  check(plan.writes[3].address == kSector + 24u && plan.writes[3].value == 0xffu &&
            plan.writes[3].width == 1u,
        "channel retired");
}

void test_channel0_blended_interpolates_toward_the_second_keyframe() {
  std::printf("test channel0_blended_interpolates_toward_the_second_keyframe\n");
  std::vector<uint8_t> ram(0x200000u);
  buildAnimation(
      ram, 0u, 1u, 4u, 0x80u, {packVertex(100u, 200u, 300u)}, {packVertex(900u, 1000u, 100u)});
  Plan plan{};
  const char *why = "none";
  check(spyro::world_animation::appendSector(RamView(ram), kSector, activeFor(0u, 1u), plan, why),
        "decodes");
  check(plan.blended == 1u && plan.direct == 0u, "took the blended form");
  // IR0 = factor<<4 = 0x800 = half of the GTE's 1.0, so each component lands halfway.
  const uint32_t written = plan.writes[0].value;
  check((written >> 21) == 500u, "x halfway");
  check(((written >> 10) & 0x7ffu) == 600u, "y halfway");
  check((written & 0x3ffu) == 200u, "z halfway");
  // A zero blend factor is the direct form, so the endpoints are reachable: factor 0xFF is very
  // nearly the second keyframe, and the interpolation must move monotonically toward it.
  std::vector<uint8_t> far(0x200000u);
  buildAnimation(
      far, 0u, 1u, 4u, 0xffu, {packVertex(100u, 200u, 300u)}, {packVertex(900u, 1000u, 100u)});
  Plan farPlan{};
  check(
      spyro::world_animation::appendSector(RamView(far), kSector, activeFor(0u, 1u), farPlan, why),
      "decodes at full factor");
  check((farPlan.writes[0].value >> 21) > 500u, "full factor moves further toward keyframe B");
}

void test_channel1_walks_colours_by_the_encoded_delta() {
  std::printf("test channel1_walks_colours_by_the_encoded_delta\n");
  std::vector<uint8_t> ram(0x200000u);
  w8(ram, kSector + 16u, 6u); // the LQ vertex count, which offsets the colour array
  // The destination delta is the word's top BYTE, counted in words: the guest's `>>22 & 0x3FC`
  // shifts bits 24..31 down and scales them by four in one step. The low 24 bits are the colour.
  const std::vector<uint32_t> source{(2u << 24) | 0x00112233u, (1u << 24) | 0x00445566u};
  buildAnimation(ram, 1u, 0u, 8u, 0u, source, {});
  Plan plan{};
  const char *why = "none";
  check(spyro::world_animation::appendSector(RamView(ram), kSector, activeFor(1u, 0u), plan, why),
        "decodes");
  const uint32_t base = kSector + 28u + 6u * 4u;
  check(plan.writes[0].address == base + 8u, "first delta applied");
  check(plan.writes[0].value == 0x00112233u, "first colour masked");
  check(plan.writes[1].address == base + 12u, "delta accumulates rather than resets");
  check(plan.writes[1].value == 0x00445566u, "second colour masked");
}

void test_channel3_feeds_two_destination_streams() {
  std::printf("test channel3_feeds_two_destination_streams\n");
  std::vector<uint8_t> ram(0x200000u);
  // The layout word packs three word-counted 8-bit fields, each scaled by four as it is extracted:
  // bits 24..31 and 0..7 sum to the first stream's origin, bits 8..15 separate the second from it.
  const uint32_t layout = (3u << 24) | (1u << 8) | 2u;
  w32(ram, kSector + 20u, layout);
  const std::vector<uint32_t> source{(1u << 24) | 0x00aabbccu, 0x77ddeeffu};
  buildAnimation(ram, 3u, 0u, 8u, 0u, source, {});
  Plan plan{};
  const char *why = "none";
  check(spyro::world_animation::appendSector(RamView(ram), kSector, activeFor(3u, 0u), plan, why),
        "decodes");
  const uint32_t first = kSector + 28u + ((layout >> 22) & 0x3fcu) + ((layout << 2) & 0x3fcu);
  const uint32_t second = first + ((layout >> 6) & 0x3fcu);
  check(plan.writes.size() == 3u, "one pair plus the stamp");
  check(plan.writes[0].address == first + 4u, "first stream");
  check(plan.writes[0].value == 0x00aabbccu, "first stream word is masked");
  check(plan.writes[1].address == second + 4u, "second stream");
  // The second stream's word keeps its top byte: it is the GTE colour code, which the blended form
  // passes through the RGB FIFO untouched.
  check(plan.writes[1].value == 0x77ddeeffu, "second stream word is not masked");
}

void test_blended_colour_matches_the_gte_depth_cue() {
  std::printf("test blended_colour_matches_the_gte_depth_cue\n");
  // DPCS with sf=1: each channel moves from the source colour toward the far colour by IR0/4096.
  // Both operands live in the GTE's 12.4 colour space, so a byte 0x40 is written as 0x400.
  const uint32_t half = spyro::world_animation::dpcs(0x00204060u, {0x400, 0x400, 0x400}, 0x800);
  check((half & 0xffu) == (0x60u + 0x40u) / 2u, "red halfway to the far colour");
  check(((half >> 8) & 0xffu) == (0x40u + 0x40u) / 2u, "green already at the far colour");
  check(((half >> 16) & 0xffu) == (0x20u + 0x40u) / 2u, "blue halfway to the far colour");
  check((spyro::world_animation::dpcs(0xab000000u, {0, 0, 0}, 0) >> 24) == 0xabu,
        "the colour code byte rides through");
  // Saturation is the hardware's, not C++'s: a far colour far above the source clamps at 255.
  const uint32_t clamped = spyro::world_animation::dpcs(0x00ffffffu, {0xff0, 0xff0, 0xff0}, 0x1000);
  check((clamped & 0xffu) == 0xffu, "clamps at the byte ceiling");
}

void test_malformed_animation_data_refuses_rather_than_guessing() {
  std::printf("test malformed_animation_data_refuses_rather_than_guessing\n");
  {
    // A payload length that is not a whole number of elements does not terminate the guest's own
    // loop either, so it is a broken model of the data rather than a case to absorb.
    std::vector<uint8_t> ram(0x200000u);
    buildAnimation(ram, 0u, 0u, 6u, 0u, {0u, 0u}, {});
    expectRefusal(ram, activeFor(0u, 0u), "animation_stride");
  }
  {
    std::vector<uint8_t> ram(0x200000u);
    buildAnimation(ram, 0u, 0u, 0u, 0u, {}, {});
    expectRefusal(ram, activeFor(0u, 0u), "animation_stride");
  }
  {
    // Channel 3 consumes source words in pairs.
    std::vector<uint8_t> ram(0x200000u);
    buildAnimation(ram, 3u, 0u, 4u, 0u, {0u}, {});
    expectRefusal(ram, activeFor(3u, 0u), "animation_stride");
  }
  {
    // A payload pointer that leaves RAM must be named, not clamped into range.
    std::vector<uint8_t> ram(0x200000u);
    buildAnimation(ram, 0u, 0u, 8u, 0u, {0u, 0u}, {});
    w32(ram, kAnimation + 8u, 0x7ff000u);
    expectRefusal(ram, activeFor(0u, 0u), "animation_payload");
  }
}

void test_a_retired_channel_does_nothing() {
  std::printf("test a_retired_channel_does_nothing\n");
  std::vector<uint8_t> ram(0x200000u);
  buildAnimation(ram, 0u, 0u, 4u, 0u, {0xdeadbeefu}, {});
  Plan plan{};
  const char *why = "none";
  // All four bytes at the retired sentinel: the whole sector is quiet.
  check(spyro::world_animation::appendSector(RamView(ram), kSector, 0xffffffffu, plan, why),
        "quiet sector decodes");
  check(plan.writes.empty() && plan.channels == 0u, "and yields no writes at all");
  // 0x80 is the boundary: the guest's test is `byte < 0x80`, so 0x80 itself is inactive.
  check(spyro::world_animation::appendSector(RamView(ram), kSector, 0xffffff80u, plan, why),
        "boundary index decodes");
  check(plan.writes.empty(), "0x80 is not a live channel");
  check(!r32(ram, kSector + 28u), "nothing was written to guest memory by decoding");
}

} // namespace

int main() {
  test_channel0_direct_copies_vertices();
  test_channel0_blended_interpolates_toward_the_second_keyframe();
  test_channel1_walks_colours_by_the_encoded_delta();
  test_channel3_feeds_two_destination_streams();
  test_blended_colour_matches_the_gte_depth_cue();
  test_malformed_animation_data_refuses_rather_than_guessing();
  test_a_retired_channel_does_nothing();
  std::printf("%d check(s), %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
