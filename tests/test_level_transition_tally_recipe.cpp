#include "core.h"
#include "level_transition_tally_recipe.h"
#include "testutil.h"

#include <cmath>
#include <memory>

namespace {

using spyro::level_transition_tally::Plan;
using spyro::level_transition_tally::State;

constexpr std::uint32_t kHudMobyCursor = 0x80075710u;
constexpr std::uint32_t kMobySize = 0x58u;

State baseState() {
  State state;
  state.nextLevelId = 11; // Artisans' first level
  state.levelName = "STONE HILL";
  state.ticks = 0;
  state.gemTotal = 100;
  state.chestDuration = 160;
  state.previousLevelGems = 60;
  state.gemsBeforeEntry = 20;
  state.gemsSinceEntry = 40;
  for (std::size_t i = 0; i < state.sine.size(); ++i) {
    const double radians = 2.0 * 3.14159265358979323846 * (double)i / 256.0;
    state.sine[i] = (std::int16_t)std::lround(std::sin(radians) * 4096.0);
  }
  return state;
}

// The caption is chosen from the level id, and the three cases are easy to get subtly wrong: a
// homeworld is any id ending in 0, a boss is level 4 of the first six homeworlds, and 63 —
// Gnasty Gnorc — is a boss despite being past that range.
void test_the_caption_names_the_right_kind_of_destination() {
  CHECK(spyro::level_transition_tally::captionText(10, "ARTISANS") == "RETURNING HOME...");
  CHECK(spyro::level_transition_tally::captionText(11, "STONE HILL") == "ENTERING STONE HILL...");
  CHECK(spyro::level_transition_tally::captionText(14, "TOASTY") == "CONFRONTING TOASTY...");
  // 64 ends in 4 but is past 60, so it is NOT a boss…
  CHECK(spyro::level_transition_tally::captionText(64, "X") == "ENTERING X...");
  // …while 63 is, by the guest's own special case.
  CHECK(spyro::level_transition_tally::captionText(63, "GNASTY GNORC") ==
        "CONFRONTING GNASTY GNORC...");
}

void test_the_level_name_index_is_six_per_homeworld() {
  CHECK(spyro::level_transition_tally::levelNameIndex(11) == 1);
  CHECK(spyro::level_transition_tally::levelNameIndex(20) == 6);
  CHECK(spyro::level_transition_tally::levelNameIndex(21) == 7);
}

// The treasure half of the screen does not exist before tick 64: no counter, no gems, no chest.
void test_nothing_of_the_treasure_block_exists_before_its_tick() {
  State state = baseState();
  state.ticks = 63;
  const Plan before = spyro::level_transition_tally::plan(state);
  CHECK(!before.hasTreasure);
  CHECK(before.gems.empty());
  CHECK(!before.caption.layout.glyphs.empty());
  state.ticks = 64;
  CHECK(spyro::level_transition_tally::plan(state).hasTreasure);
}

// The counter holds this level's haul, ramps it down to zero, then shows the running total and
// ramps that up. Getting the two ramps the same way round is the whole point of the screen.
void test_the_counter_counts_this_level_down_and_the_total_up() {
  State state = baseState();
  const std::int32_t collected = state.previousLevelGems - state.gemsBeforeEntry; // 40
  state.ticks = 100;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == collected);
  // The ramp runs for min((gemsSinceEntry + 1) * 2, 64) ticks from 128, so 64 here: at its start
  // the full haul is still shown and at its end nothing is.
  state.ticks = 128;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == collected);
  state.ticks = 192;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == 0);
  // Past 224 the screen shows the running total less this level's haul, then ramps it back up.
  state.ticks = 240;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == state.gemTotal - collected);
  state.ticks = 272;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == state.gemTotal - collected);
  state.ticks = 336;
  CHECK(spyro::level_transition_tally::plan(state).displayedCounter == state.gemTotal);
}

// A larger total shifts the whole treasure block left, one step per decimal digit.
void test_a_wider_total_shifts_the_block_left() {
  State state = baseState();
  state.ticks = 200;
  state.gemTotal = 9;
  const std::int32_t narrow = spyro::level_transition_tally::plan(state).counter.layout.end.x;
  state.gemTotal = 9999;
  const std::int32_t wide = spyro::level_transition_tally::plan(state).counter.layout.end.x;
  CHECK(wide < narrow);
}

// The caption holds still through the middle of the screen and eases at both ends, so a sampled
// middle tick must read exactly 32 while the first tick does not.
void test_the_caption_eases_in_and_holds() {
  State state = baseState();
  state.ticks = 200;
  CHECK(spyro::level_transition_tally::plan(state).caption.layout.glyphs[0].position.y == 32);
  state.ticks = 0;
  CHECK(spyro::level_transition_tally::plan(state).caption.layout.glyphs[0].position.y == 0);
}

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();
};

// The refusal has to be atomic across the WHOLE screen, not per string: a tally that wrote its
// caption and then ran out of arena would leave a caption floating over the previous frame.
void test_submit_refuses_the_whole_screen_when_the_arena_is_short() {
  Harness h;
  // Room for two Mobys, which the caption alone exceeds.
  const std::uint32_t cursor = 0x80010000u + 2u * kMobySize;
  h.core->mem_w32(kHudMobyCursor, cursor);
  CHECK(!spyro::level_transition_tally::submit(h.core.get()));
  CHECK(h.core->mem_r32(kHudMobyCursor) == cursor);
}

} // namespace

int main() {
  RUN(the_caption_names_the_right_kind_of_destination);
  RUN(the_level_name_index_is_six_per_homeworld);
  RUN(nothing_of_the_treasure_block_exists_before_its_tick);
  RUN(the_counter_counts_this_level_down_and_the_total_up);
  RUN(a_wider_total_shifts_the_block_left);
  RUN(the_caption_eases_in_and_holds);
  RUN(submit_refuses_the_whole_screen_when_the_arena_is_short);
  return pt_summary();
}
