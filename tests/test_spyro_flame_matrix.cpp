// The flame renderer 0x80058D64 has no source for its own orientation: 0x80023AC4 hands it the
// composed Spyro matrix at 0x80024110, gated on the enable byte at g_SpyroFlame+0x9A. The port lost
// that publication when it replaced 0x80023AC4 natively, and the failure was silent — the five
// words stayed zero, every flame point projected onto the flame origin, and the producer still
// reported a full census of faces. These cases pin both answers of the gate.
#include "core.h"
#include "game.h"
#include "spyro_flame_matrix.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

constexpr uint32_t kFlame = 0x800786c8u;
constexpr uint32_t kEnable = kFlame + 0x9au;
constexpr uint32_t kMatrix = kFlame + 0xb8u;

const std::array<uint32_t, 5> kWords = {
    0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u, 0x0000999au};

std::unique_ptr<Game> fixture(uint8_t enable) {
  auto game = std::make_unique<Game>();
  for (uint32_t i = 0; i < kWords.size(); ++i) {
    game->core.mem_w32(kMatrix + i * 4u, 0xdeadbeefu);
  }
  game->core.mem_w8(kEnable, enable);
  return game;
}

void test_publishes_all_five_words_when_the_gate_is_set() {
  const auto game = fixture(1);
  CHECK(spyro_flame_matrix_publish(&game->core, kWords));
  for (uint32_t i = 0; i < kWords.size(); ++i) {
    CHECK_EQ(game->core.mem_r32(kMatrix + i * 4u), kWords[i]);
  }
}

// The gate is what makes the flame keep the orientation it was lit with while Spyro's own matrix
// moves on, so a publication that ignored it would look like a working flame and be wrong.
void test_clear_gate_leaves_the_previous_matrix_untouched() {
  const auto game = fixture(0);
  CHECK(!spyro_flame_matrix_publish(&game->core, kWords));
  for (uint32_t i = 0; i < kWords.size(); ++i) {
    CHECK_EQ(game->core.mem_r32(kMatrix + i * 4u), 0xdeadbeefu);
  }
}

void test_null_core_refuses() {
  CHECK(!spyro_flame_matrix_publish(nullptr, kWords));
}

} // namespace

int main() {
  RUN(publishes_all_five_words_when_the_gate_is_set);
  RUN(clear_gate_leaves_the_previous_matrix_untouched);
  RUN(null_core_refuses);
  return pt_summary();
}
