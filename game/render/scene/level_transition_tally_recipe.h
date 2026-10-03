#pragma once

// level_transition_tally_recipe — func_8001973C, the level-transition tally screen.
//
// It is the one unowned piece of stage 1's producer func_8001A050. It draws three strings and two
// families of Moby: the "ENTERING <level>..." caption, a "TREASURE FOUND" / "TOTAL TREASURE"
// caption, a running gem counter, the gems flying into the chest, and the chest itself. Everything
// it emits goes into the descending g_HudMobys arena, which the already-owned HUD chain then draws.
//
// The plan half is pure — it takes the guest scalars and produces what to draw — so the whole
// tick-keyed schedule is testable without a running game. The submit half performs the arena writes
// through the shared hud_text owner.

#include "hud_text_builder.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Core;

namespace spyro::level_transition_tally {

// One entry of g_LevelTransGems paired with its class from g_RecentGemsCollected.
struct Gem {
  bool active = false;
  std::int32_t xOffset = 0;
  std::int32_t yOffset = 0;
  std::uint8_t rotX = 0;
  std::uint8_t rotY = 0;
  std::uint8_t rotZ = 0;
  std::uint16_t mobyClass = 0;
};

struct State {
  std::int32_t nextLevelId = 0;
  std::int32_t ticks = 0;
  std::int32_t gemTotal = 0;
  std::int32_t chestDuration = 0;
  // g_LevelGemCount[g_PreviousLevelIndex] and D_8007587C — the level's gem count and what the
  // player already had walking in. Their difference is what this screen tallies up.
  std::int32_t previousLevelGems = 0;
  std::int32_t gemsBeforeEntry = 0;
  std::int32_t gemsSinceEntry = 0;
  std::string levelName;
  std::array<Gem, 32> gems{};
  // SINE_8's table. It is part of the state rather than read inside the plan so the whole schedule
  // stays a pure function of its inputs and a test can drive the easing curves directly.
  std::array<std::int16_t, 256> sine{};
};

// A Moby the tally writes directly rather than through the text builder.
struct Sprite {
  std::uint16_t mobyClass = 0;
  hud_text::Point3 position;
  std::uint8_t rotX = 0;
  std::uint8_t rotY = 0;
  std::uint8_t rotZ = 0;
  std::uint8_t shadeIndex = 0;
};

// One laid-out string plus the shade it is drawn with. Every one of them gets the same per-glyph
// wobble afterward, with the glyph index restarting at zero for each.
struct Text {
  hud_text::Layout layout;
  std::uint8_t shadeIndex = 0;
};

struct Plan {
  Text caption;
  // Present only from tick 64, when the treasure half of the screen appears.
  bool hasTreasure = false;
  Text treasureCaption;
  Text counter;
  std::int32_t displayedCounter = 0;
  std::vector<Sprite> gems;
  Sprite chest;
};

// The caption's own text, chosen from the level being entered.
std::string captionText(std::int32_t nextLevelId, const std::string &levelName);

// The index into g_LevelNames for a level id. Exposed because it is the one piece of arithmetic
// here that silently produces a plausible wrong answer when it is off by one.
std::int32_t levelNameIndex(std::int32_t nextLevelId);

Plan plan(const State &state);

State read(Core *core);

// Append the whole tally to the HUD arena. Returns false and writes nothing when the arena cannot
// hold it, so a refusal leaves the frame exactly as it was.
bool submit(Core *core);

} // namespace spyro::level_transition_tally
