#include "hud_text_builder.h"

#include "core.h"

namespace spyro::hud_text {
namespace {

// include/moby.h's MobyClass values for the characters the two builders know.
constexpr std::uint16_t kNumber0 = 260u;
constexpr std::uint16_t kLetterA = 426u;
constexpr std::uint16_t kExclamation = 75u;
constexpr std::uint16_t kApostrophe = 76u;
constexpr std::uint16_t kPercent = 272u;
constexpr std::uint16_t kSlash = 277u;
constexpr std::uint16_t kQuestion = 278u;
constexpr std::uint16_t kPlus = 317u;
constexpr std::uint16_t kCaret = 321u;
constexpr std::uint16_t kPeriod = 327u;

// Moby field offsets, all three anchored by code already in this tree: 0x36 is actor_scene_oracle's
// kMobyClass, 0x47 is moby_shadow_recipe's kMobyDepthOffset, 0x58 is actor_scene_builder's
// kMobySize.
constexpr std::uint32_t kMobySize = 0x58u;
constexpr std::uint32_t kPositionX = 0x0Cu;
constexpr std::uint32_t kPositionY = 0x10u;
constexpr std::uint32_t kPositionZ = 0x14u;
constexpr std::uint32_t kClass = 0x36u;
constexpr std::uint32_t kDepthOffset = 0x47u;
constexpr std::uint32_t kSpecularMetalType = 0x4Fu;
constexpr std::uint32_t kRenderRadius = 0x50u;

// g_HudMobys. GamestateDraw points it at the transient pool's far end each frame and every builder
// walks it DOWNWARD, so it is a bump allocator running backwards, not an array base.
constexpr std::uint32_t kHudMobyCursor = 0x80075710u;

constexpr std::uint32_t kRamBegin = 0x80010000u;
constexpr std::uint32_t kRamEnd = 0x80200000u;

bool isDigit(char ch) {
  return ch >= '0' && ch <= '9';
}

bool isUpper(char ch) {
  return ch >= 'A' && ch <= 'Z';
}

} // namespace

Layout layoutCounter(std::string_view text, Point3 position, std::int32_t spaceWidth) {
  Layout layout;
  for (const char ch : text) {
    if (ch != ' ') {
      std::uint16_t mobyClass = kPeriod; // the guest's own fallback for anything it does not know
      if (isDigit(ch)) {
        mobyClass = (std::uint16_t)(kNumber0 + (ch - '0'));
      } else if (isUpper(ch)) {
        mobyClass = (std::uint16_t)(kLetterA + (ch - 'A'));
      } else if (ch == '/') {
        mobyClass = kSlash;
      } else if (ch == '?') {
        mobyClass = kQuestion;
      } else if (ch == '%') {
        mobyClass = kPercent;
      } else if (ch == '^') {
        mobyClass = kCaret;
      } else if (ch == '+') {
        mobyClass = kPlus;
      }
      layout.glyphs.push_back({position, mobyClass});
    }
    // Fixed pitch: the advance is outside the space test, so a space still costs a full cell.
    position.x += spaceWidth;
  }
  layout.end = position;
  return layout;
}

Layout
layoutCaption(std::string_view text, Point3 position, Point3 spacing, std::int32_t spaceWidth) {
  Layout layout;
  // The guest calls this `isCapital`, but it means "the previous glyph occupied a full cell". It
  // starts set, so the first glyph of a string is never dropped to the narrow line.
  bool fullWidth = true;
  for (const char ch : text) {
    if (ch == ' ') {
      // Three quarters of the narrow advance, with the guest's own bias so the shift rounds toward
      // zero for a negative spacing rather than away from it.
      std::int32_t spaceSize = spacing.x * 3;
      fullWidth = true;
      if (spaceSize < 0) {
        spaceSize += 3;
      }
      position.x += spaceSize >> 2;
      continue;
    }
    if (ch == '!' || ch == '?') {
      fullWidth = true;
    }
    Point3 glyph = position;
    if (!fullWidth) {
      glyph.y += spacing.y;
      glyph.z = spacing.z;
    }
    std::uint16_t mobyClass = 0;
    if (isDigit(ch)) {
      mobyClass = (std::uint16_t)(kNumber0 + (ch - '0'));
    } else if (isUpper(ch)) {
      mobyClass = (std::uint16_t)(kLetterA + (ch - 'A'));
    } else if (ch == '!') {
      mobyClass = kExclamation;
    } else if (ch == ',') {
      mobyClass = kApostrophe;
    } else if (ch == '?') {
      mobyClass = kQuestion;
    } else if (ch == '.') {
      mobyClass = kPeriod;
    } else {
      // Anything else becomes an apostrophe raised by two thirds of the narrow advance. The guest
      // subtracts, and screen Y grows downward here, so this lifts it.
      mobyClass = kApostrophe;
      glyph.y -= spacing.x * 2 / 3;
    }
    layout.glyphs.push_back({glyph, mobyClass});
    position.x += fullWidth ? spaceWidth : spacing.x;
    // A digit is the only character that leaves the next glyph full width.
    fullWidth = isDigit(ch);
  }
  layout.end = position;
  return layout;
}

// The arena grows downward into the transient pool, so a string fits only when the cursor is inside
// RAM and has that many bytes left below it.
bool fits(Core *core, std::size_t glyphCount) {
  const std::uint32_t cursor = core->mem_r32(kHudMobyCursor);
  const std::uint64_t bytes = (std::uint64_t)glyphCount * kMobySize;
  return cursor >= kRamBegin && cursor < kRamEnd && bytes <= cursor - kRamBegin;
}

std::vector<std::uint32_t> append(Core *core, const Layout &layout, std::uint8_t shadeIndex) {
  std::vector<std::uint32_t> written;
  // Checked before the first write, so a refusal leaves the arena exactly as it was.
  if (!fits(core, layout.glyphs.size())) {
    return written;
  }
  std::uint32_t moby = core->mem_r32(kHudMobyCursor);
  written.reserve(layout.glyphs.size());
  for (const Glyph &glyph : layout.glyphs) {
    moby -= kMobySize;
    for (std::uint32_t offset = 0; offset < kMobySize; offset += 4u) {
      core->mem_w32(moby + offset, 0u);
    }
    core->mem_w32(moby + kPositionX, (std::uint32_t)glyph.position.x);
    core->mem_w32(moby + kPositionY, (std::uint32_t)glyph.position.y);
    core->mem_w32(moby + kPositionZ, (std::uint32_t)glyph.position.z);
    core->mem_w16(moby + kClass, glyph.mobyClass);
    core->mem_w8(moby + kDepthOffset, 0x7Fu);
    core->mem_w8(moby + kSpecularMetalType, shadeIndex);
    core->mem_w8(moby + kRenderRadius, 0xFFu);
    written.push_back(moby);
  }
  core->mem_w32(kHudMobyCursor, moby);
  return written;
}

} // namespace spyro::hud_text
