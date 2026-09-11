#include "core.h"
#include "hud_text_builder.h"
#include "testutil.h"

#include <memory>
#include <string>

namespace {

using spyro::hud_text::Layout;
using spyro::hud_text::Point3;

constexpr std::uint32_t kHudMobyCursor = 0x80075710u;
constexpr std::uint32_t kMobySize = 0x58u;

std::string classes(const Layout &layout) {
  std::string out;
  for (const auto &glyph : layout.glyphs) {
    out += std::to_string(glyph.mobyClass) + " ";
  }
  return out;
}

// The two builders differ in exactly the places that matter: '/' is a slash to the counter and an
// apostrophe to the caption, and the caption's fallback lifts the glyph while the counter's does
// not. A shared class table would silently pass one builder's punctuation to the other.
void test_the_two_builders_are_not_the_same_table() {
  const Layout counter = spyro::hud_text::layoutCounter("7/9", {0, 0, 0}, 16);
  CHECK(classes(counter) == "267 277 269 ");
  const Layout caption = spyro::hud_text::layoutCaption("7/9", {0, 0, 0}, {8, 4, 100}, 16);
  // 277 (slash) must NOT appear; the caption maps '/' to the apostrophe class.
  CHECK(classes(caption) == "267 76 269 ");
}

// Fixed pitch means a space still costs a cell and produces no glyph.
void test_the_counter_advances_through_spaces_without_emitting_one() {
  const Layout layout = spyro::hud_text::layoutCounter("A B", {10, 0, 0}, 16);
  CHECK(layout.glyphs.size() == 2u);
  CHECK(layout.glyphs[0].position.x == 10);
  CHECK(layout.glyphs[1].position.x == 42);
  CHECK(layout.end.x == 58);
}

// The caption's narrow-glyph state machine: the first glyph is always full width, a letter leaves
// the next one narrow, a digit leaves it full width again, and a narrow glyph is offset onto its
// own line by the spacing's y and z.
void test_the_caption_drops_the_glyph_after_a_letter_and_not_after_a_digit() {
  const Layout layout = spyro::hud_text::layoutCaption("AB1C", {0, 0, 7}, {8, 4, 100}, 16);
  CHECK(layout.glyphs.size() == 4u);
  // 'A' is first, so full width at the caller's own position, and it advances by spaceWidth.
  CHECK(layout.glyphs[0].position.y == 0);
  CHECK(layout.glyphs[0].position.z == 7);
  CHECK(layout.glyphs[1].position.x == 16);
  // 'B' follows a letter, so it is narrow: offset by spacing.y and moved to spacing.z.
  CHECK(layout.glyphs[1].position.y == 4);
  CHECK(layout.glyphs[1].position.z == 100);
  // …and a narrow glyph advances by the narrow width, not spaceWidth.
  CHECK(layout.glyphs[2].position.x == 24);
  // '1' follows a letter so it is itself narrow, but being a digit it leaves 'C' full width again.
  CHECK(layout.glyphs[2].position.y == 4);
  CHECK(layout.glyphs[3].position.y == 0);
  CHECK(layout.glyphs[3].position.z == 7);
}

// A space resets the run and advances by three quarters of the narrow width, and '!' forces the
// following glyph full width even mid-word.
void test_the_caption_space_and_bang_reset_the_run() {
  const Layout layout = spyro::hud_text::layoutCaption("A A", {0, 0, 0}, {8, 4, 0}, 16);
  // 'A' at 0 advances by 16; the space adds (8*3)>>2 == 6; the second 'A' is full width at 22.
  CHECK(layout.glyphs[1].position.x == 22);
  CHECK(layout.glyphs[1].position.y == 0);
  const Layout bang = spyro::hud_text::layoutCaption("A!B", {0, 0, 0}, {8, 4, 0}, 16);
  // '!' would have been narrow after 'A', but it forces full width for itself and leaves 'B'
  // narrow.
  CHECK(bang.glyphs[1].position.y == 0);
  CHECK(bang.glyphs[1].mobyClass == 75u);
  CHECK(bang.glyphs[2].position.y == 4);
}

// Negative spacing: the guest biases the space advance before shifting so it rounds toward zero.
// Without the bias, -6 >> 2 is -2 rather than -1, and a right-to-left caption creeps.
void test_a_negative_space_advance_rounds_toward_zero() {
  const Layout layout = spyro::hud_text::layoutCaption("A A", {0, 0, 0}, {-2, 0, 0}, 16);
  CHECK(layout.glyphs[1].position.x == 15);
}

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();
};

void test_append_writes_the_arena_downward_and_moves_the_cursor() {
  Harness h;
  constexpr std::uint32_t kCursor = 0x80100000u;
  h.core->mem_w32(kHudMobyCursor, kCursor);
  const Layout layout = spyro::hud_text::layoutCounter("42", {5, 6, 7}, 16);
  const auto written = spyro::hud_text::append(h.core.get(), layout, 11u);
  CHECK(written.size() == 2u);
  // Newest first: the first glyph lands one Moby BELOW the old cursor, the next one below that.
  CHECK(written[0] == kCursor - kMobySize);
  CHECK(written[1] == kCursor - 2u * kMobySize);
  CHECK(h.core->mem_r32(kHudMobyCursor) == kCursor - 2u * kMobySize);
  CHECK(h.core->mem_r32(written[0] + 0x0Cu) == 5u);
  CHECK(h.core->mem_r32(written[0] + 0x10u) == 6u);
  CHECK(h.core->mem_r32(written[0] + 0x14u) == 7u);
  CHECK(h.core->mem_r16(written[0] + 0x36u) == 264u); // '4'
  CHECK(h.core->mem_r16(written[1] + 0x36u) == 262u); // '2'
  CHECK(h.core->mem_r8(written[0] + 0x47u) == 0x7Fu);
  CHECK(h.core->mem_r8(written[0] + 0x4Fu) == 11u);
  CHECK(h.core->mem_r8(written[0] + 0x50u) == 0xFFu);
}

// The refusal must be atomic: a string that cannot fit leaves the cursor and the arena untouched,
// rather than writing the glyphs that happened to fit before running off the bottom of RAM.
void test_append_refuses_atomically_when_the_arena_cannot_hold_the_string() {
  Harness h;
  constexpr std::uint32_t kCursor = 0x80010000u + kMobySize; // room for exactly one Moby
  h.core->mem_w32(kHudMobyCursor, kCursor);
  const Layout layout = spyro::hud_text::layoutCounter("AB", {0, 0, 0}, 16);
  CHECK(spyro::hud_text::append(h.core.get(), layout, 11u).empty());
  CHECK(h.core->mem_r32(kHudMobyCursor) == kCursor);
  CHECK(h.core->mem_r16(kCursor - kMobySize + 0x36u) == 0u);
}

} // namespace

int main() {
  RUN(the_two_builders_are_not_the_same_table);
  RUN(the_counter_advances_through_spaces_without_emitting_one);
  RUN(the_caption_drops_the_glyph_after_a_letter_and_not_after_a_digit);
  RUN(the_caption_space_and_bang_reset_the_run);
  RUN(a_negative_space_advance_rounds_toward_zero);
  RUN(append_writes_the_arena_downward_and_moves_the_cursor);
  RUN(append_refuses_atomically_when_the_arena_cannot_hold_the_string);
  return pt_summary();
}
