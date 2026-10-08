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

constexpr std::uint32_t kQueue = spyro::hud_text::kShadedMobyQueue;

// The wobble every caller shares: glyph i's rotation is COSINE_8((phase + 12 i) & 0xFF) >> 7, the
// low byte of a signed shift, so a negative cosine stores its two's complement.
void test_wobble_applies_the_phase_and_the_per_glyph_step() {
  Harness h;
  constexpr std::uint32_t kCosine = 0x8006CC78u;
  for (std::uint32_t i = 0; i < 256u; ++i) {
    h.core->mem_w16(kCosine + i * 2u, (std::uint16_t)(std::int16_t)(i < 128u ? 0x1000 : -0x1000));
  }
  const std::vector<std::uint32_t> glyphs = {0x80100000u, 0x80100058u};
  spyro::hud_text::wobble(h.core.get(), glyphs, 124);
  CHECK(h.core->mem_r8(glyphs[0] + 0x46u) == 0x20u);        // index 124: +0x1000 >> 7
  CHECK(h.core->mem_r8(glyphs[1] + 0x46u) == 0xE0u);        // index 136: -0x1000 >> 7 == -32
  spyro::hud_text::wobble(h.core.get(), glyphs, 124 + 256); // the phase wraps at 256
  CHECK(h.core->mem_r8(glyphs[0] + 0x46u) == 0x20u);
}

// The fairy menu's amplitude, `COSINE_8(...) * 3 >> 9` (draw.c:2094): the multiply happens before
// the shift, so a cosine that the plain `>> 7` maps to 32 maps to 0x1000 * 3 >> 9 == 24.
void test_scaled_wobble_multiplies_before_it_shifts() {
  Harness h;
  constexpr std::uint32_t kCosine = 0x8006CC78u;
  for (std::uint32_t i = 0; i < 256u; ++i) {
    h.core->mem_w16(kCosine + i * 2u, (std::uint16_t)(std::int16_t)(i < 128u ? 0x1000 : -0x1000));
  }
  const std::vector<std::uint32_t> glyphs = {0x80100000u, 0x80100058u};
  spyro::hud_text::wobbleScaled(h.core.get(), glyphs, 124, {3, 9});
  CHECK(h.core->mem_r8(glyphs[0] + 0x46u) == 24u);
  CHECK(h.core->mem_r8(glyphs[1] + 0x46u) == 0xE8u); // -0x1000 * 3 >> 9 == -24
  spyro::hud_text::wobbleScaled(h.core.get(), glyphs, 124, spyro::hud_text::kPlainWobble);
  CHECK(h.core->mem_r8(glyphs[0] + 0x46u) == 0x20u); // the default scale is the shared wobble
}

void test_enqueue_appends_after_the_entries_and_terminates() {
  Harness h;
  h.core->mem_w32(kQueue, 0x80077FECu);
  h.core->mem_w32(kQueue + 4u, 0u);
  h.core->mem_w32(kQueue + 8u, 0xDEADBEEFu); // stale, past the terminator
  const std::vector<std::uint32_t> mobys = {0x80100000u, 0x80100058u};
  CHECK(spyro::hud_text::enqueueShaded(h.core.get(), mobys));
  CHECK(h.core->mem_r32(kQueue + 4u) == 0x80100000u);
  CHECK(h.core->mem_r32(kQueue + 8u) == 0x80100058u);
  CHECK(h.core->mem_r32(kQueue + 12u) == 0u);
}

// Room is counted for the terminator too: 255 entries leave exactly one free slot, which is the
// terminator's, so even a single further Moby is refused and nothing is written.
void test_enqueue_refuses_when_the_terminator_would_not_fit() {
  Harness h;
  for (std::uint32_t slot = 0; slot < 255u; ++slot) {
    h.core->mem_w32(kQueue + slot * 4u, 0x80080000u);
  }
  const std::vector<std::uint32_t> one = {0x80100000u};
  CHECK(!spyro::hud_text::shadedQueueFits(h.core.get(), 1u));
  CHECK(!spyro::hud_text::enqueueShaded(h.core.get(), one));
  CHECK(h.core->mem_r32(kQueue + 255u * 4u) == 0u);
  CHECK(spyro::hud_text::shadedQueueFits(h.core.get(), 0u));
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
  RUN(wobble_applies_the_phase_and_the_per_glyph_step);
  RUN(scaled_wobble_multiplies_before_it_shifts);
  RUN(enqueue_appends_after_the_entries_and_terminates);
  RUN(enqueue_refuses_when_the_terminator_would_not_fit);
  return pt_summary();
}
