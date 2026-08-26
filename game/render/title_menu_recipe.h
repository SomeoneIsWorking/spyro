#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace spyro::title_menu_recipe {

struct SpriteCommand {
  int32_t x = 0;
  int32_t y = 0;
  int32_t sprite = 0;
  uint32_t style = 0;

  bool operator==(const SpriteCommand &) const = default;
};

struct Mode1Input {
  uint32_t substate = 0;
  uint32_t optionSelected = 0;
  uint32_t subTick = 0;
  int32_t cardSelected = 0;
};

struct Recipe {
  static constexpr size_t kCapacity = 9;

  std::array<SpriteCommand, kCapacity> commands{};
  size_t size = 0;

  void append(int32_t x, int32_t y, int32_t sprite, uint32_t style);
};

// Native transcription of TitlescreenDraw's TSM_Menu arm in OV_5B800
// (0x8007D0C8..0x8007D3F4). The recipe is independent of presentation so the
// shipping producer and its hermetic falsifiers exercise one implementation.
Recipe buildMode1(const Mode1Input &input);

// Exact ordered-command comparator shared by the live retained-body oracle and
// its hermetic positive/negative controls. On failure, mismatch is the first
// differing command, or the shared prefix length when only the counts differ.
bool sameCommands(const Recipe &expected, const Recipe &observed, size_t &mismatch);

} // namespace spyro::title_menu_recipe
