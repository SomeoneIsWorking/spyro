#pragma once

#include <array>
#include <cstdint>

namespace spyro::field_collectables_recipe {

constexpr uint32_t kMaxShadedMobys = 12u;
constexpr uint32_t kMaxSprites = 32u;

enum class Status : uint8_t { Ready, CompletedGemTextUnowned, InvalidCount };

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
};

struct Tile {
  uint8_t u = 0;
  uint8_t v = 0;
  uint16_t clut = 0;
  uint16_t tpage = 0;
};

struct Sprite {
  Rect rect{};
  Tile tile{};
  uint8_t r = 0x80u;
  uint8_t g = 0x80u;
  uint8_t b = 0x80u;
};

struct State {
  bool flightLevel = false;
  uint8_t gemDisplay = 0;
  uint8_t dragonDisplay = 0;
  uint8_t lifeDisplay = 0;
  uint8_t eggDisplay = 0;
  uint8_t keyDisplay = 0;
  uint32_t keyFlag = 0;
  int32_t lifeOrbCount = 0;
  int32_t eggCount = 0;
  uint32_t eggPhase = 0;
  uint32_t specularTime = 0;
  std::array<Rect, kMaxSprites> rects{};
  std::array<Tile, 10> tiles{};
  std::array<int16_t, 256> cosine{};
};

struct Recipe {
  Status status = Status::Ready;
  std::array<uint32_t, kMaxShadedMobys> shadedMobys{};
  uint32_t shadedCount = 0;
  std::array<Sprite, kMaxSprites> sprites{};
  uint32_t spriteCount = 0;
};

// Exact non-text branches of retail FIELD collectables producer 0x80019300.
// Gem-display state 4 also constructs formatted text mobys; that branch stays
// an atomic refusal until its no-cap text state owner is connected.
Recipe derive(const State &state);

} // namespace spyro::field_collectables_recipe
