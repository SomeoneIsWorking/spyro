// GS_Dragon is drawn by 0x8001CFDC, which is eight authored compositions rather than one producer.
// The port aborted on this stage entirely, so these cases pin which layers each state composes and,
// just as importantly, which it deliberately leaves out — a branch that drew one layer too many
// would look plausible and be wrong.
#include "core.h"
#include "dragon_scene_recipe.h"
#include "game.h"
#include "testutil.h"

#include <algorithm>
#include <memory>

namespace {

using spyro::dragon_scene::Producer;
using spyro::dragon_scene::Status;

constexpr uint32_t kCutscene = 0x80077030u;
constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kMobyArray = 0x80100000u;
constexpr uint32_t kMobyBytes = 0x58u;
constexpr uint32_t kDragon = 0x80120000u;
constexpr uint32_t kSpyro = 0x80121000u;
constexpr uint32_t kCutsceneDragon = 0x80122000u;

bool has(const std::vector<Producer> &producers, Producer wanted) {
  return std::find(producers.begin(), producers.end(), wanted) != producers.end();
}

size_t indexOf(const std::vector<Producer> &producers, Producer wanted) {
  return (size_t)(std::find(producers.begin(), producers.end(), wanted) - producers.begin());
}

// Two level mobys, the second a crystal fragment still on screen, plus the three cutscene actors.
std::unique_ptr<Game> dragonFixture(uint32_t state, int32_t ticks = 0, int32_t fade = 0) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  core.mem_w32(kLevelMobys, kMobyArray);
  core.mem_w16(kMobyArray + 54u, 10u);
  core.mem_w8(kMobyArray + 72u, 0u);
  core.mem_w16(kMobyArray + kMobyBytes + 54u, 251u);
  core.mem_w8(kMobyArray + kMobyBytes + 72u, 1u);
  core.mem_w8(kMobyArray + kMobyBytes * 2u + 72u, 0xffu);
  core.mem_w8(kDragon + 72u, 2u);
  core.mem_w32(kCutscene + 0x28u, state);
  core.mem_w32(kCutscene + 0x3cu, (uint32_t)ticks);
  core.mem_w32(kCutscene + 0x44u, (uint32_t)fade);
  core.mem_w32(kCutscene + 0x88u, kSpyro);
  core.mem_w32(kCutscene + 0x8cu, kCutsceneDragon);
  core.mem_w32(kCutscene + 0x90u, kDragon);
  return game;
}

void test_missing_core_is_refused() {
  CHECK(spyro::dragon_scene::plan(nullptr, {}).status == Status::InvalidCore);
}

void test_state_zero_runs_the_whole_field_chain() {
  const auto game = dragonFixture(0);
  const auto state = spyro::dragon_scene::read(&game->core);
  const auto plan = spyro::dragon_scene::plan(&game->core, state);
  CHECK(plan.status == Status::Ready);
  CHECK(has(plan.producers, Producer::FieldChain));
  // The one state that does NOT build its own lists, because 0x800521C0 fills them.
  CHECK(!plan.lists.writeDraw);
  CHECK(!plan.explicitDrawSource);
  CHECK(indexOf(plan.producers, Producer::QueueMobys) <
        indexOf(plan.producers, Producer::FieldChain));
}

void test_early_states_draw_the_rescued_dragon_from_an_explicit_list() {
  const auto game = dragonFixture(1);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(plan.explicitDrawSource);
  CHECK_EQ(plan.lists.draw.size(), (size_t)1);
  CHECK_EQ(plan.lists.draw[0], kDragon);
  // Only the crystal fragment reaches the shaded list; the ordinary Moby beside it does not.
  CHECK_EQ(plan.lists.shaded.size(), (size_t)1);
  CHECK_EQ(plan.lists.shaded[0], kMobyArray + kMobyBytes);
  CHECK(has(plan.producers, Producer::SpyroModel));
  CHECK(!has(plan.producers, Producer::Secondary));
}

void test_a_hidden_rescued_dragon_leaves_the_draw_list_empty() {
  const auto game = dragonFixture(1);
  // A state byte at or above 0x80 is retail's own "not drawn" mark.
  game->core.mem_w8(kDragon + 72u, 0x80u);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK_EQ(plan.lists.draw.size(), (size_t)0);
  // The distinguishing part: the list is still WRITTEN, so the previous state's pointers cannot
  // survive into this frame as a stale draw.
  CHECK(plan.lists.writeDraw);
}

void test_state_three_adds_the_cutscene_spyro() {
  const auto game = dragonFixture(3);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK_EQ(plan.lists.draw.size(), (size_t)2);
  CHECK_EQ(plan.lists.draw[1], kSpyro);
}

void test_state_four_draws_spyros_shadow_but_not_his_model() {
  const auto game = dragonFixture(4, 10);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  // The cutscene Spyro is in the draw list, so drawing the player model too would double him.
  CHECK(!has(plan.producers, Producer::SpyroModel));
  CHECK(has(plan.producers, Producer::SpyroShadow));
  CHECK_EQ(plan.lists.draw.size(), (size_t)2);
  CHECK(has(plan.producers, Producer::RescuedText));
}

void test_state_four_stops_building_the_rescued_text_after_sixty_ticks() {
  const auto game = dragonFixture(4, 60);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(!has(plan.producers, Producer::RescuedText));
  CHECK(!has(plan.producers, Producer::CopyHudMobys));
  CHECK(has(plan.producers, Producer::Regular));
}

void test_state_six_has_no_regular_pass_at_all() {
  const auto game = dragonFixture(6);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(!has(plan.producers, Producer::Regular));
  CHECK(!has(plan.producers, Producer::MobyShadows));
  CHECK(has(plan.producers, Producer::Shaded));
  CHECK(!plan.explicitDrawSource);
  // The three HUD counters are the whole shaded list here.
  CHECK_EQ(plan.lists.shaded.size(), (size_t)3);
}

void test_final_state_returns_to_the_level_array_after_sixteen_ticks() {
  const auto early = dragonFixture(7, 15);
  const auto earlyPlan =
      spyro::dragon_scene::plan(&early->core, spyro::dragon_scene::read(&early->core));
  CHECK(!has(earlyPlan.producers, Producer::Regular));
  CHECK_EQ(earlyPlan.lists.shaded.size(), (size_t)3);

  const auto late = dragonFixture(7, 16);
  const auto latePlan =
      spyro::dragon_scene::plan(&late->core, spyro::dragon_scene::read(&late->core));
  CHECK(has(latePlan.producers, Producer::QueueMobys));
  CHECK(has(latePlan.producers, Producer::Regular));
  // The only branch besides state 0 that reaches the secondary pass.
  CHECK(has(latePlan.producers, Producer::Secondary));
  CHECK(!latePlan.explicitDrawSource);
  CHECK(!latePlan.lists.writeShaded);
}

void test_the_final_state_fades_after_its_border() {
  const auto game = dragonFixture(7, 20, 4);
  game->core.mem_w32(0x8007570Cu, 1u);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(has(plan.producers, Producer::ScreenFade));
  // Every other state fades before the border; this one is the exception.
  CHECK(indexOf(plan.producers, Producer::ScreenBorder) <
        indexOf(plan.producers, Producer::ScreenFade));

  const auto early = dragonFixture(1, 0, 4);
  early->core.mem_w32(0x8007570Cu, 1u);
  const auto earlyPlan =
      spyro::dragon_scene::plan(&early->core, spyro::dragon_scene::read(&early->core));
  CHECK(indexOf(earlyPlan.producers, Producer::ScreenFade) <
        indexOf(earlyPlan.producers, Producer::ScreenBorder));
}

void test_a_zero_fade_composes_no_fade_layer() {
  const auto game = dragonFixture(1, 0, 0);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(!has(plan.producers, Producer::ScreenFade));
}

void test_an_unknown_state_is_named_rather_than_drawn_empty() {
  const auto game = dragonFixture(8);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(plan.status == Status::UnknownState);
  CHECK_EQ(plan.producers.size(), (size_t)0);
}

void test_an_unterminated_level_array_is_refused() {
  const auto game = dragonFixture(1);
  game->core.mem_w8(kMobyArray + kMobyBytes * 2u + 72u, 0u);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  CHECK(plan.status == Status::UnterminatedMobyArray);
}

void test_commit_writes_both_lists_with_their_terminators() {
  const auto game = dragonFixture(3);
  const auto plan = spyro::dragon_scene::plan(&game->core, spyro::dragon_scene::read(&game->core));
  spyro::dragon_scene::commit(&game->core, plan);
  Core &core = game->core;
  CHECK_EQ(core.mem_r32(spyro::dragon_scene::kDrawList), kDragon);
  CHECK_EQ(core.mem_r32(spyro::dragon_scene::kDrawList + 4u), kSpyro);
  CHECK_EQ(core.mem_r32(spyro::dragon_scene::kDrawList + 8u), 0u);
  CHECK_EQ(core.mem_r32(spyro::dragon_scene::kShadedList), kMobyArray + kMobyBytes);
  CHECK_EQ(core.mem_r32(spyro::dragon_scene::kShadedList + 4u), 0u);
}

} // namespace

int main() {
  RUN(missing_core_is_refused);
  RUN(state_zero_runs_the_whole_field_chain);
  RUN(early_states_draw_the_rescued_dragon_from_an_explicit_list);
  RUN(a_hidden_rescued_dragon_leaves_the_draw_list_empty);
  RUN(state_three_adds_the_cutscene_spyro);
  RUN(state_four_draws_spyros_shadow_but_not_his_model);
  RUN(state_four_stops_building_the_rescued_text_after_sixty_ticks);
  RUN(state_six_has_no_regular_pass_at_all);
  RUN(final_state_returns_to_the_level_array_after_sixteen_ticks);
  RUN(the_final_state_fades_after_its_border);
  RUN(a_zero_fade_composes_no_fade_layer);
  RUN(an_unknown_state_is_named_rather_than_drawn_empty);
  RUN(an_unterminated_level_array_is_refused);
  RUN(commit_writes_both_lists_with_their_terminators);
  return pt_summary();
}
