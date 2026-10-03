// SPDX-License-Identifier: GPL-3.0-or-later
// Spyro 1's logo: THE GAME'S OWN WORDMARK SPRITE, read live while the title screen draws it.
//
// The logo is not a file this port decodes and not a rectangle of a framebuffer. It is sprite 0 of
// Spyro 1's own title-screen sprite table, which the game's overlay draws from a texture and a CLUT
// the game itself uploaded to VRAM. So this file states the gate the guest's own title screen is
// decided by and the sprite record that IS the wordmark, and `PanelLogo` snapshots exactly that
// texture through exactly that palette out of the panel's own live VRAM.
//
// EVERY ADDRESS HERE IS MEASURED, and every one of them can be re-read:
//
//  1. THE EMITTER. The title overlay's draw is 0x8007CEE4 (the decompilation names it
//     `func_8007CEE4`, "titlescreen overlay draw", called from GamestateDraw.c:108); its SPRITE
//     half reaches DrawSprite through 0x8007CD38, which computes each element's address as
//     `&DAT_8006facc + id*8` for the two halfwords and `&DAT_8006fad0 + id*8` for the four bytes.
//     That is the layout `SpriteRec::read` reproduces: tpage, clut, w, h, u, v.
//
//  2. THE SPRITE TABLE is 64 records of 8 bytes at 0x8006FACC. The bound is the game's own, not a
//     guess: ids 0..63 are coherent (this logo, the banner pair at id 1, three 255x15 text pages at
//     tpage 0x1C/0x1D/0x1E, sixteen 16x16 glyphs at tpage 0x1A) and id 64 is already garbage.
//
//  3. THE LOGO IS RECORD 0, at 0x8006FACC, and it is in the main image as data:
//     `external/open-spyro/asm/data/data.data.s:6005` has `/* 602CC 8006FACC 9800E07F */ .word
//     0x7FE00098`, which is tpage 0x0098, clut 0x7FE0 — and the live table at the title screen
//     reads back the same word followed by FF 80 00 00, i.e. w=255, h=128, u=0, v=0.
//
//  4. THE TEXPAGE SAYS THE DEPTH: bits 7-8 of 0x0098 are 01, so the wordmark is 8-BIT INDEXED,
//  which
//     is why its pixels sit two to a 16-bit VRAM word and why reading them as 15-bit colour
//     produced per-texel noise inside a correct silhouette. Decoded by the port's own renderer
//     arithmetic (game/render/fx_title_menu.cpp's push2dQuad), the logo lives at VRAM page x =
//     (0x98 & 0xF)*64 = 512, y = ((0x98 >> 4) & 1)*256 = 256, and its palette at x = (0x7FE0 &
//     0x3F)*16 = 512, y = 0x7FE0 >> 6 = 511.
//
//  5. THE GATE IS RETAIL'S OWN. The overlay's state block is reached through the pointer global at
//     0x80075680 — the word holding a pointer, recovered as the image's single
//     `lui $rX,0x8007` + `sw 0x5680($rX)` writer at 0x80014A38 — and the block's first int,
//     m_CurrentTick, is what the overlay itself tests: `m_CurrentTick < 1100` before the logo and
//     `>= 1169` after it (decompilation overlays/titlescreen.c:100-157). The threshold stated here
//     is 1170, the first tick at which the logo is on screen.
//
//  6. WHAT THE RESULT MUST LOOK LIKE, so it can be checked rather than believed: the wordmark is
//     "SPYRO THE DRAGON" in lavender 3-D letters over an orange/yellow sunburst, about 160x87
//     texels at native 320x240, upper centre. `scratch/screenshots/present_900.png` is the game
//     drawing it.
namespace spyro1 {

// `const`, not `constexpr`: `TitleLogoFacts` holds a `std::vector` of sprite records, which is not
// a literal type on this toolchain, so a `constexpr` definition cannot be constant-evaluated.
inline const spyro::TitleLogoFacts kLogoFacts{
    .valid = true,
    .gate = {.pointerGlobal = 0x80075680u, .tickOffset = 0u, .tickValue = 1170u},
    // ONE PART: record 0 IS the whole wordmark. Measured: a snapshot of record 0 alone is the
    // "SPYRO THE DRAGON" sunburst logo, correct colours and clean (scratch/picker_verify/
    // logo_word.png, 2026-10-03). The records that share its page are the menu's OTHER elements and
    // are left out on purpose: id 1 (0x8006FAD4) is the 148x128 banner at tpage 0x009A, ids 2-4 and
    // 8-10 are the 80x48 menu thumbnails at v=128 and v=176, ids 5-7 are their mirrored copies on
    // the next pages, ids 11-12 are the 128x16 pulsing strip, ids 13-23 are the 16x16 4-bit glyphs,
    // and ids 24-31 are the 255x15 text pages at tpage 0x001C..0x001E. 128 of the record's 255: in
    // 8-bit mode a page is 512 texels wide, and the banner's page origin (record 1, tpage 0x009A)
    // is x=640 while this one is x=512 — 128 texels to the right. So the right-hand 127 columns of
    // this rectangle ARE the banner's texture, which is why an unclipped snapshot shows the
    // wordmark beside a purple plate with a gold edge.
    .parts = {{.record = 0x8006FACCu, .clipWidth = 128u}},
};

} // namespace spyro1