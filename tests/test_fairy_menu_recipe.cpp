#include "fairy_menu_recipe.h"
#include "testutil.h"

#include <array>

// The fairy dialogue, derived from the guest's own switch (draw.c:2041-2310). Every expectation is
// a coordinate or a count the handler states, so a recipe that drifts from it goes red here.

using spyro::fairy_menu::Kind;
using spyro::fairy_menu::State;

namespace {

// g_FairyDialogueBoxSizes as the image holds it (0x8006F350): {x, x2, y, y2} per page.
constexpr std::array<spyro::fairy_menu::BoxRecord, 8> kBoxes = {{
    {0x28, 0x128, 0x24, 0x7E},
    {0x1E, 0x132, 0x24, 0x78},
    {0x28, 0x128, 0x24, 0x7E},
    {0x1E, 0x132, 0x24, 0x78},
    {0x1E, 0x132, 0x24, 0x78},
    {0x1E, 0x132, 0x24, 0x78},
    {0x28, 0x128, 0x24, 0x7E},
    {0x28, 0x128, 0x24, 0x7E},
}};

State stateFor(std::uint32_t page, std::uint32_t selected = 0) {
  State state;
  state.state = 1;
  state.page = page;
  state.selected = selected;
  state.boxes = kBoxes;
  return state;
}

// The number of glyphs the guest's `textLen` wobbles on each page: sizeof(literal) - 1 with the
// spaces removed, i.e. the characters that become mobys.
std::size_t glyphsOf(const char *text) {
  std::size_t n = 0;
  for (const char *c = text; *c != 0; ++c) {
    n += *c != ' ' ? 1u : 0u;
  }
  return n;
}

} // namespace

static void test_a_frame_that_is_not_the_dialogue_draws_the_world_only(void) {
  State state = stateFor(0);
  state.state = 0;
  const auto recipe = spyro::fairy_menu::derive(state);
  CHECK(recipe.kind == Kind::WorldOnly);
  CHECK(recipe.captions.empty());
  CHECK(recipe.border.empty());
}

static void test_the_main_page_lays_four_captions_and_wobbles_the_selected_one(void) {
  for (std::uint32_t selected = 0; selected < 3; ++selected) {
    const auto recipe = spyro::fairy_menu::derive(stateFor(0, selected));
    CHECK(recipe.kind == Kind::Dialogue);
    CHECK_EQ(recipe.captions.size(), 4u);
    CHECK(recipe.wobbled.has_value());
    CHECK_EQ(*recipe.wobbled, 1u + selected);
  }
  const auto recipe = spyro::fairy_menu::derive(stateFor(0, 0));
  CHECK_EQ(recipe.captions[0].y, 50);
  CHECK_EQ(recipe.captions[1].y, 74);
  CHECK_EQ(recipe.captions[2].y, 93);
  CHECK_EQ(recipe.captions[3].y, 112);
  CHECK_EQ(recipe.captions[0].x, 64);
  CHECK_EQ(recipe.captions[1].x, 76);
}

static void test_the_menu_offset_moves_the_box_and_every_caption(void) {
  State state = stateFor(0, 0);
  const auto left = spyro::fairy_menu::derive(state);
  state.offsetX = 0xB0;
  const auto right = spyro::fairy_menu::derive(state);
  CHECK_EQ(right.panel.x0, left.panel.x0 + 0xB0);
  CHECK_EQ(right.panel.x1, left.panel.x1 + 0xB0);
  CHECK_EQ(right.panel.y0, left.panel.y0);
  for (std::size_t i = 0; i < left.captions.size(); ++i) {
    CHECK_EQ(right.captions[i].x, left.captions[i].x + 0xB0);
  }
  CHECK_EQ(left.panel.x0, 0x28);
  CHECK_EQ(left.panel.x1, 0x128);
}

static void test_the_box_comes_from_the_guests_table_per_page(void) {
  for (std::uint32_t page = 0; page < 8; ++page) {
    const auto recipe = spyro::fairy_menu::derive(stateFor(page, 0));
    CHECK(recipe.kind == Kind::Dialogue);
    CHECK_EQ(recipe.panel.x0, kBoxes[page].x);
    CHECK_EQ(recipe.panel.x1, kBoxes[page].x2);
    CHECK_EQ(recipe.panel.y0, kBoxes[page].y);
    CHECK_EQ(recipe.panel.y1, kBoxes[page].y2);
    CHECK_EQ(recipe.border.size(), 4u);
  }
}

static void test_the_outline_walks_top_right_bottom_left(void) {
  const auto recipe = spyro::fairy_menu::derive(stateFor(0, 0));
  const auto &p = recipe.panel;
  CHECK_EQ(recipe.border[0].x0, p.x0);
  CHECK_EQ(recipe.border[0].x1, p.x1);
  CHECK_EQ(recipe.border[0].y0, p.y0);
  CHECK_EQ(recipe.border[0].y1, p.y0);
  CHECK_EQ(recipe.border[1].x0, p.x1);
  CHECK_EQ(recipe.border[1].y1, p.y1);
  CHECK_EQ(recipe.border[2].x0, p.x1);
  CHECK_EQ(recipe.border[2].x1, p.x0);
  CHECK_EQ(recipe.border[3].x0, p.x0);
  CHECK_EQ(recipe.border[3].y1, p.y0);
}

static void test_the_titled_pages_wobble_their_title_and_stack_three_lines(void) {
  for (const std::uint32_t page : {1u, 3u, 4u, 5u}) {
    const auto recipe = spyro::fairy_menu::derive(stateFor(page));
    CHECK_EQ(recipe.captions.size(), page == 1u ? 4u : 5u); // pages above 1 add the slot caption
    CHECK(recipe.wobbled.has_value());
    CHECK_EQ(*recipe.wobbled, 0u);
    CHECK_EQ(recipe.captions[1].y, 76);
    CHECK_EQ(recipe.captions[2].y, 93);
    CHECK_EQ(recipe.captions[3].y, 110);
  }
}

static void test_pages_above_one_show_the_used_card_slot(void) {
  const auto first = spyro::fairy_menu::derive(stateFor(2));
  CHECK_EQ(first.captions.size(), 2u); // SAVING... and SLOT 1
  CHECK_EQ(first.captions.back().y, 29);
  CHECK_EQ(first.captions.back().x, 48);
  State second = stateFor(2);
  second.cardSlot = 1;
  second.offsetX = 0xB0;
  const auto other = spyro::fairy_menu::derive(second);
  // The guest does not add the menu offset to slot 2's x (draw.c:2307).
  CHECK_EQ(other.captions.back().x, 230);
  CHECK(other.captions.back().text != first.captions.back().text);
  // Pages 0 and 1 have no slot caption.
  CHECK_EQ(spyro::fairy_menu::derive(stateFor(1)).captions.size(), 4u);
  CHECK_EQ(spyro::fairy_menu::derive(stateFor(0)).captions.size(), 4u);
}

static void test_the_failed_page_wobbles_retry_or_abort(void) {
  const auto retry = spyro::fairy_menu::derive(stateFor(6, 0));
  const auto abort = spyro::fairy_menu::derive(stateFor(6, 1));
  CHECK_EQ(*retry.wobbled, 1u);
  CHECK_EQ(*abort.wobbled, 2u);
  CHECK_EQ(retry.captions[0].y, 53);
  CHECK_EQ(retry.captions[1].y, 93);
  CHECK_EQ(retry.captions[2].y, 112);
  CHECK_EQ(retry.captions[1].x, 135);
}

// Both ways: the pages and selections the guest's switch handles are drawn, and every one it does
// not is refused rather than given a guessed picture.
static void test_pages_and_selections_outside_the_guests_switch_are_refused(void) {
  CHECK(spyro::fairy_menu::derive(stateFor(8)).kind == Kind::Refused);
  CHECK(spyro::fairy_menu::derive(stateFor(0xFFFFFFFFu)).kind == Kind::Refused);
  CHECK(spyro::fairy_menu::derive(stateFor(0, 3)).kind == Kind::Refused);
  CHECK(spyro::fairy_menu::derive(stateFor(6, 2)).kind == Kind::Refused);
  CHECK(spyro::fairy_menu::derive(stateFor(0, 2)).kind == Kind::Dialogue);
  // A refusal carries no half-built picture.
  CHECK(spyro::fairy_menu::derive(stateFor(0, 3)).captions.empty());
  CHECK(!spyro::fairy_menu::derive(stateFor(0, 3)).wobbled.has_value());
  // The selection is irrelevant on the pages that do not read it.
  CHECK(spyro::fairy_menu::derive(stateFor(2, 99)).kind == Kind::Dialogue);
}

static void test_the_wobble_amplitude_and_shade_are_the_handlers(void) {
  CHECK_EQ(spyro::fairy_menu::kWobbleMultiplier, 3);
  CHECK_EQ(spyro::fairy_menu::kWobbleShift, 9);
  CHECK_EQ(spyro::fairy_menu::kCaptionShade, 11u);
  CHECK_EQ(spyro::fairy_menu::kPanelColour, 112u);
  CHECK_EQ(glyphsOf("NO SAVE FILE"), 10u); // sizeof("NOSAVEFILE") - 1 in the handler
  CHECK_EQ(glyphsOf("SAVING..."), 9u);
}

int main(void) {
  RUN(a_frame_that_is_not_the_dialogue_draws_the_world_only);
  RUN(the_main_page_lays_four_captions_and_wobbles_the_selected_one);
  RUN(the_menu_offset_moves_the_box_and_every_caption);
  RUN(the_box_comes_from_the_guests_table_per_page);
  RUN(the_outline_walks_top_right_bottom_left);
  RUN(the_titled_pages_wobble_their_title_and_stack_three_lines);
  RUN(pages_above_one_show_the_used_card_slot);
  RUN(the_failed_page_wobbles_retry_or_abort);
  RUN(pages_and_selections_outside_the_guests_switch_are_refused);
  RUN(the_wobble_amplitude_and_shade_are_the_handlers);
  return pt_summary();
}
