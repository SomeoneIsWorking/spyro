// SPDX-License-Identifier: GPL-3.0-or-later
// A logo is the game's OWN sprite, drawn from a texture and CLUT the game uploaded, at the moment
// the title screen's own gate says the logo is up; nothing here is authored art.
#pragma once

#include <cstdint>
#include <vector>

namespace spyro {

// The title screen's own gate: the guest keeps a pointer global, the overlay's state block hangs
// off it, and the block's tick is what retail itself tests (Spyro 1: `m_CurrentTick >= 1170`).
struct TitleLogoGate {
  std::uint32_t pointerGlobal = 0; // the guest word that HOLDS the pointer (not the pointer itself)
  std::uint32_t tickOffset = 0;    // the tick's offset inside that block
  std::uint32_t tickValue = 0;     // the tick at which the logo is up
};

// A wordmark is the logo sprite and at most a banner beside it.
inline constexpr std::size_t kTitleLogoMaximumParts = 8;

// The wordmark's own sprite records, in composition order: each address is a record in the game's
// own sprite table, read from the guest at the moment the gate is open.
struct TitleLogoPart {
  std::uint32_t record = 0; // guest address of one 8-byte sprite record
  // How much of that record's own width this part occupies, or 0 for all of it. In 8-bit mode a
  // texture page is 512 texels wide, so two sprites whose page origins are 128 texels apart share
  // the VRAM the later one reads.
  std::uint32_t clipWidth = 0;
};

struct TitleLogoFacts {
  // PanelLogo refuses invalid facts rather than snapshotting a rectangle that is not the wordmark.
  bool valid = false;
  TitleLogoGate gate{};
  std::vector<TitleLogoPart> parts{};
};

// The texpage and clut words decoded, as the guest's own renderer applies them when it hands a
// sprite to the GPU; they live here so a VRAM snapshot cannot drift from what was sampled.
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