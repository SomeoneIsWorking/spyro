// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT A TITLE'S LOGO IS, in the form the game itself keeps it.
//
// The wordmark is not a file this port decodes: it is the game's OWN sprite, drawn from a texture
// the game itself uploaded to VRAM and coloured by a CLUT the game itself uploaded, at the moment
// the title screen's own gate says the logo is up. So a logo is stated here the way the guest holds
// it — the gate to test, and the sprite records to read — and `PanelLogo` snapshots exactly those
// rectangles out of the panel's own live VRAM.
//
// Everything in here is measured from the player's own disc, in the addresses below; nothing is
// authored art and nothing is committed.
#pragma once

#include <cstdint>
#include <vector>

namespace spyro {

// THE TITLE SCREEN'S OWN GATE. The guest keeps a pointer global; the overlay's state block hangs
// off it, and the block's tick is what retail itself tests to decide whether its logo is on screen
// (Spyro 1: `m_CurrentTick >= 1170`, the `m_CurrentTick < 1100` / `>= 1169` tests in the
// decompilation's overlays/titlescreen.c). Reading the pointer and the tick rather than a derived
// "is it a title screen" flag keeps this a statement about the guest's own decision.
struct TitleLogoGate {
  std::uint32_t pointerGlobal = 0; // the guest word that HOLDS the pointer (not the pointer itself)
  std::uint32_t tickOffset = 0;    // the tick's offset inside that block
  std::uint32_t tickValue = 0;     // the tick at which the logo is up
};

// The maximum number of parts a wordmark may be composed from. A wordmark is the logo sprite and at
// most a banner beside it; a title whose logo needs more than this is not a wordmark.
inline constexpr std::size_t kTitleLogoMaximumParts = 8;

// THE WORDMARK'S OWN SPRITE RECORDS, in composition order. Each address is a record in the game's
// own sprite table, read from the guest at the moment the gate is open — the same read the game's
// own emitter makes, so the texture rectangle and palette that come back are the ones the game
// drew.
struct TitleLogoPart {
  std::uint32_t record = 0; // guest address of one 8-byte sprite record
  // How much of that record's own width this wordmark part is, or 0 for all of it. It is not a
  // preference: in 8-bit mode a texture page is 512 texels wide, so two sprites whose page origins
  // are 128 texels apart share the VRAM the later one reads, and the right-hand part of the wider
  // sprite's rectangle is the OTHER sprite's artwork. Where that happens the fact states the width
  // the wordmark actually occupies and says which record owns the rest.
  std::uint32_t clipWidth = 0;
};

struct TitleLogoFacts {
  // False until every field below is measured. PanelLogo refuses invalid facts rather than
  // snapshotting a rectangle that is not the wordmark: a panel that draws nothing is a defect the
  // log names, and a panel that draws the wrong picture is a lie the player cannot see through.
  bool valid = false;
  TitleLogoGate gate{};
  std::vector<TitleLogoPart> parts{};
};

// THE TEXPAGE AND CLUT WORDS, DECODED. These are the four expressions the guest's own renderer
// applies when it hands a sprite to the GPU (game/render/fx_title_menu.cpp's push2dQuad call, and
// the same arithmetic in the decompilation's DumpClut: `x = (clut & 0x3f) << 4, y = clut >> 6`).
// They live here so a VRAM snapshot cannot drift from what the renderer sampled: a snapshot that
// placed its rect one page off would produce a picture that is nearly, but not quite, the logo.
constexpr int texturePageX(std::uint16_t tpage) {
  return static_cast<int>(tpage & 0xFu) * 64;
}
constexpr int texturePageY(std::uint16_t tpage) {
  return static_cast<int>((tpage >> 4) & 1u) * 256;
}
// 0 = 4-bit indexed, 1 = 8-bit indexed, 2 = 15-bit direct colour. 3 is not a depth and refuses.
constexpr int texturePageDepth(std::uint16_t tpage) {
  return static_cast<int>((tpage >> 7) & 3u);
}
constexpr int paletteX(std::uint16_t clut) {
  return static_cast<int>(clut & 0x3Fu) * 16;
}
constexpr int paletteY(std::uint16_t clut) {
  return static_cast<int>((clut >> 6) & 0x1FFu);
}

} // namespace spyro