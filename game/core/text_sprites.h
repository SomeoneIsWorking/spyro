#pragma once

#include <cstdint>

namespace spyro {

enum class TextGlyphKind : uint8_t {
  Digit,
  Letter,
  Exclamation,
  Comma,
  Question,
  Period,
  Fallback,
};

constexpr TextGlyphKind classifyTextGlyph(uint8_t character) {
  if (character >= '0' && character <= '9') {
    return TextGlyphKind::Digit;
  }
  if (character >= 'A' && character <= 'Z') {
    return TextGlyphKind::Letter;
  }
  switch (character) {
  case '!':
    return TextGlyphKind::Exclamation;
  case ',':
    return TextGlyphKind::Comma;
  case '?':
    return TextGlyphKind::Question;
  case '.':
    return TextGlyphKind::Period;
  default:
    return TextGlyphKind::Fallback;
  }
}

constexpr uint16_t textGlyphClass(TextGlyphKind kind, uint8_t character) {
  switch (kind) {
  case TextGlyphKind::Digit:
    return static_cast<uint16_t>(character + 0xD4u);
  case TextGlyphKind::Letter:
    return static_cast<uint16_t>(character + 0x169u);
  case TextGlyphKind::Exclamation:
    return 0x4Bu;
  case TextGlyphKind::Comma:
  case TextGlyphKind::Fallback:
    return 0x4Cu;
  case TextGlyphKind::Question:
    return 0x116u;
  case TextGlyphKind::Period:
    return 0x147u;
  }
  return 0;
}

constexpr bool textGlyphForcesCapital(TextGlyphKind kind) {
  return kind == TextGlyphKind::Exclamation || kind == TextGlyphKind::Question;
}

constexpr bool textGlyphLeavesCapital(TextGlyphKind kind) {
  return kind == TextGlyphKind::Digit;
}

} // namespace spyro
