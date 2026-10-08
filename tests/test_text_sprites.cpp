#include "text_sprites.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

} // namespace

int main() {
  using spyro::TextGlyphKind;

  expect(spyro::classifyTextGlyph('0') == TextGlyphKind::Digit, "digit kind");
  expect(spyro::textGlyphClass(TextGlyphKind::Digit, '0') == 0x104u, "first digit class");
  expect(spyro::textGlyphClass(TextGlyphKind::Digit, '9') == 0x10Du, "last digit class");
  expect(spyro::classifyTextGlyph('A') == TextGlyphKind::Letter, "letter kind");
  expect(spyro::textGlyphClass(TextGlyphKind::Letter, 'A') == 0x1AAu, "first letter class");
  expect(spyro::textGlyphClass(TextGlyphKind::Letter, 'Z') == 0x1C3u, "last letter class");

  expect(spyro::textGlyphClass(spyro::classifyTextGlyph('!'), '!') == 0x4Bu, "exclamation class");
  expect(spyro::textGlyphClass(spyro::classifyTextGlyph(','), ',') == 0x4Cu, "comma class");
  expect(spyro::textGlyphClass(spyro::classifyTextGlyph('?'), '?') == 0x116u, "question class");
  expect(spyro::textGlyphClass(spyro::classifyTextGlyph('.'), '.') == 0x147u, "period class");
  expect(spyro::textGlyphClass(spyro::classifyTextGlyph('/'), '/') == 0x4Cu, "fallback class");

  expect(spyro::textGlyphForcesCapital(TextGlyphKind::Exclamation), "bang forces capital");
  expect(spyro::textGlyphForcesCapital(TextGlyphKind::Question), "question forces capital");
  expect(!spyro::textGlyphForcesCapital(TextGlyphKind::Period), "period does not force capital");
  expect(spyro::textGlyphLeavesCapital(TextGlyphKind::Digit), "digit leaves capital");
  expect(!spyro::textGlyphLeavesCapital(TextGlyphKind::Letter), "letter clears capital");

  if (failures != 0) {
    return 1;
  }
  std::puts("text sprite semantics: PASS");
  return 0;
}
