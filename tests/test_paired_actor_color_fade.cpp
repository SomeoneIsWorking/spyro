#include "paired_actor_color_fade.h"

#include "gte_color_ops.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
using spyro::paired_actor_color_fade::active;
using spyro::paired_actor_color_fade::apply;

unsigned checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "paired_actor_color_fade: %s (check %u)\n", message, checks);
    std::abort();
  }
}

uint32_t channel(uint32_t word, unsigned index) {
  return (word >> (index * 8u)) & 0xffu;
}

void testAZeroHighByteIsTheOrdinaryPath() {
  require(!active(0x00FFFFFFu), "a zero strength selected the fade");
  require(active(0x01000000u), "a strength of one did not select the fade");
  std::array<uint32_t, 2> table{0x00112233u, 0x00445566u};
  const auto before = table;
  apply(0x00FFFFFFu, table);
  require(table == before, "the ordinary path rewrote the material table");
}

void testTheStrongestFadeAlmostReachesTheFarColour() {
  // IR0 is the top byte shifted left four, so the strongest fade the word can encode is 0xFF0 out
  // of 4096 -- near the far colour but never on it. Asserting the exact endpoint would be asserting
  // an interpolation this arm cannot express.
  std::array<uint32_t, 2> table{0x00000000u, 0x00FFFFFFu};
  apply(0xFF204060u, table);
  for (const uint32_t entry : table) {
    require(channel(entry, 0) >= 0x5Eu && channel(entry, 0) <= 0x60u,
            "the strongest fade did not arrive near the far colour's red");
    require(channel(entry, 1) >= 0x3Eu && channel(entry, 1) <= 0x40u,
            "the strongest fade did not arrive near the far colour's green");
    require(channel(entry, 2) >= 0x1Eu && channel(entry, 2) <= 0x20u,
            "the strongest fade did not arrive near the far colour's blue");
  }
}

void testAStrongerFadeMovesFurther() {
  // The two answers have to be able to differ, or the strength byte would be decoration.
  std::array<uint32_t, 1> weak{0x00000000u};
  std::array<uint32_t, 1> strong{0x00000000u};
  apply(0x10FFFFFFu, weak);
  apply(0x80FFFFFFu, strong);
  require(channel(strong[0], 0) > channel(weak[0], 0), "a stronger fade did not move further");
}

void testEachChannelTakesItsOwnFieldOfTheControlWord() {
  // A test that restated the shifts would agree with a transposed implementation, so this one
  // drives one channel of the far colour up and the other two to zero.
  std::array<uint32_t, 1> red{0x00000000u};
  apply(0xFF0000FFu, red);
  require(channel(red[0], 0) > 0xF0u && channel(red[0], 1) == 0u && channel(red[0], 2) == 0u,
          "the far colour's low byte did not land on red alone");
  std::array<uint32_t, 1> blue{0x00000000u};
  apply(0xFFFF0000u, blue);
  require(channel(blue[0], 0) == 0u && channel(blue[0], 1) == 0u && channel(blue[0], 2) > 0xF0u,
          "the far colour's third byte did not land on blue alone");
}

void testAPartialFadeLandsBetweenTheTwo() {
  std::array<uint32_t, 1> table{0x00000000u};
  apply(0x08FFFFFFu, table);
  for (unsigned i = 0; i < 3; ++i) {
    const uint32_t value = channel(table[0], i);
    require(value > 0u && value < 0xFFu, "a half fade landed on one of its endpoints");
  }
}

void testTheFadeIsTheSharedGteOperation() {
  // The arm is one DPCS per entry. Restating its arithmetic here would let the two drift and still
  // agree with each other, so the expectation runs the shared operation instead.
  std::array<uint32_t, 1> table{0x00204060u};
  apply(0x40112233u, table);
  const uint32_t expected = spyro::gte_color::dpcs(0x00204060u, {0x330, 0x220, 0x110}, 0x40 << 4);
  require(table[0] == expected, "the fade is not the shared DPCS over the control word's fields");
}

} // namespace

int main() {
  testAZeroHighByteIsTheOrdinaryPath();
  testTheStrongestFadeAlmostReachesTheFarColour();
  testAStrongerFadeMovesFurther();
  testEachChannelTakesItsOwnFieldOfTheControlWord();
  testAPartialFadeLandsBetweenTheTwo();
  testTheFadeIsTheSharedGteOperation();
  std::printf("paired_actor_color_fade: %u checks passed\n", checks);
  return 0;
}
