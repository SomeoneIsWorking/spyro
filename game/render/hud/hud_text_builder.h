#pragma once

// hud_text_builder — the guest's two HUD text builders, 0x80017FE4 and 0x800181AC.
//
// Both lay a string into the descending g_HudMobys arena: one Moby per non-space character, class
// selected from the character, then the HUD moby chain draws them. They are NOT one routine with a
// flag. 0x80017FE4 is the fixed-pitch counter builder — every character advances by the same width
// and it knows '/', '%', '^' and '+'. 0x800181AC is the proportional caption builder — it carries a
// full-width/narrow state across characters, drops narrow glyphs by a spacing offset, collapses
// runs of spaces to three quarters of the narrow advance, and knows '!', ',', '?' and '.'. Giving
// them one shared class table would put a '/' in a caption and an apostrophe in a counter.
//
// The layout half is pure so it can be tested without a Core; the append half performs the guest's
// own arena writes.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

struct Core;

namespace spyro::hud_text {

// g_SonyImage.m_ShadedMobys (0x800720F4): the zero-terminated list of Mobys the shaded pass
// 0x80022A2C draws, which the HUD text builders' callers append their glyphs to.
inline constexpr std::uint32_t kShadedMobyQueue = 0x800720F4u;
inline constexpr std::uint32_t kShadedMobyCapacity = 256u;

struct Point3 {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

struct Glyph {
  Point3 position;
  std::uint16_t mobyClass = 0;
};

struct Layout {
  std::vector<Glyph> glyphs;
  // Where the guest left the caller's position vector. Both builders advance it in place and their
  // callers read it back — the tally moves the second caption left by 16 from exactly this value.
  Point3 end;
};

// 0x80017FE4. Fixed pitch: every character, space or not, advances x by spaceWidth.
Layout layoutCounter(std::string_view text, Point3 position, std::int32_t spaceWidth);

// 0x800181AC. Proportional, with the narrow-glyph state machine described above.
Layout
layoutCaption(std::string_view text, Point3 position, Point3 spacing, std::int32_t spaceWidth);

// Whether the arena can hold that many glyphs right now. Exposed so a producer that must decide
// atomically — refuse the whole frame before writing anything — asks the same question `append`
// answers, instead of keeping a second copy of the bound.
bool fits(Core *core, std::size_t glyphCount);

// Append one laid-out string to the HUD moby arena, newest first, exactly as the guest does: take
// the cursor, step it back one Moby per glyph, zero the record and write the five fields the
// builders set. Returns the addresses written, in the order the guest wrote them, so a caller that
// must post-process its own glyphs — the level-transition tally rotates each one — can do so
// without re-deriving where they landed. Refuses and writes nothing when the arena cannot hold the
// string.
std::vector<std::uint32_t> append(Core *core, const Layout &layout, std::uint8_t shadeIndex);

// The wobble every HUD text caller gives its glyphs: glyph i's rotation about z is
// COSINE_8((phase + i * 12) & 0xFF) >> 7 (0x8006CC78 is COSINE_8's table). The callers differ only
// in the phase they pass: the demo text `g_LevelTicks * 4` (0x80018990-0x800189A0), the
// completed-gem tally `g_Hud` steady ticks * 4, the level-transition tally its tick * 2.
void wobble(Core *core, std::span<const std::uint32_t> glyphs, std::int32_t phase);

// The wobble's amplitude: rotation = (COSINE_8 * multiplier) >> shift. Every caller above uses the
// plain `>> 7`; the fairy's menu (0x8001D718) scales by 3 and shifts by 9.
struct WobbleScale {
  std::int32_t multiplier = 1;
  std::int32_t shift = 7;
};
inline constexpr WobbleScale kPlainWobble{1, 7};
void wobbleScaled(Core *core,
                  std::span<const std::uint32_t> glyphs,
                  std::int32_t phase,
                  WobbleScale scale);

// Whether the shaded-Moby queue can take that many more entries and its terminator. Exposed for the
// same reason as `fits`: a producer that must refuse before writing anything asks the question
// `enqueueShaded` answers.
bool shadedQueueFits(Core *core, std::size_t count);

// Append Mobys to the first empty slot of the shaded-Moby queue and terminate it after the last.
// Refuses and writes nothing when `shadedQueueFits` says no.
bool enqueueShaded(Core *core, std::span<const std::uint32_t> mobys);

} // namespace spyro::hud_text
