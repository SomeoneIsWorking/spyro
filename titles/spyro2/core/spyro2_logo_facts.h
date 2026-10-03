#pragma once

#include "title_logo_facts.h"

namespace spyro2 {

// NO VERIFIED LOGO FACTS YET, AND THE MEASUREMENTS SAY WHY. See spyro1_logo_facts.h for the form a
// filled header takes: which guest state names the logo, where its texture rectangle is, and how
// its texels are encoded.
//
// THE SPYRO 1 RECIPE DOES NOT TRANSFER. Spyro 1's title wordmark is one record of a 64-entry sprite
// table the guest's title overlay draws; Spyro 2 has no such table anywhere in the disc image, the
// overlay bundle or the machine.
//
// MEASURED 2026-10-03 on SCUS_944.25, paused ON ITS OWN TITLE SCREEN (frame 1019 of a standalone
// boot; whole machine saved as `scratch/s2state/title.bin`, picture `scratch/s2state/t1.png`):
//
//  * NOT A MOVIE. The native Fmv owner never runs: there is no `[fmv] begin` line in a run that
//    reaches the title screen, and no `[cd]` streaming either — the disc is quiet once the boot
//    reads are done, which a playing STR could not be. Neither disc image contains a `.STR`
//    directory entry (whole-image scan for `.STR\0` in an ISO directory record: 0 in both
//    SCUS_944.25 and SCUS_944.67). So the MDEC/STR route is not where this title screen comes from.
//  * NO SPRITE TABLE. Scanning all 2 MiB of guest RAM for Spyro 1's eight-byte record (texel
//  pointer,
//    CLUT and texture page packed as one word, then w, h, u, v) finds no run of six consecutive
//    records and only 27 loose words with the shape at all. The 34 overlay modules the disc's index
//    at 0x23000 points at (0x8006DA40, 0x8006DB50, ... 0x110 bytes each) hold none either, and the
//    index block itself has no record-shaped word.
//  * THE MACHINE'S TEXTURE MEMORY IS FULL, not empty. The saved GPU section is 1 MiB of populated
//    texture data. An earlier claim that guest VRAM was empty was wrong: it rested on `provat`
//    reporting `<never written>`, and `provat` reports exactly that for SPYRO 1 at its own title
//    screen, where the logo's texels are demonstrably resident. `provat` traces GP0 prims and does
//    not see DMA-written texture pages, so it cannot answer "is this in VRAM" at all.
//  * THE PLATE IS NOT IN RAM. The on-screen plate (purple SPYRO, gold Ripto's Rage!, screen rect
//    (160,10)-(360,110)) sampled at 15-bit and matched as a 12-texel run against every byte of the
//    whole saved machine gives 0 hits — so it is not a 15-bit guest RAM image.
//  * WHAT 0x8019C000 ACTUALLY HOLDS. That staging buffer is real, is 15-bit (8- and 4-bit decodes
//    are noise), 512 texels wide, and does contain wordmark art: rows 85..125 are red SPYRO 2
//    letters over a swoosh with Ripto's Rage beneath (`scratch/s2state/band_alpha.png`). But it is
//    a vertical slice — letters cut off top and bottom, high-entropy data above and below,
//    unchanged after 30 more frames — and its art is NOT the plate the title screen shows.
//
//  * THE OVERLAY BUNDLE WAS PUT THROUGH GHIDRA AND DOES NOT HOLD THE EMITTER. The disc's entry 1 is
//  a
//    34-entry index of module bases (0x8006DA40, 0x8006DB50, ... 0x110 bytes apart); matching the
//    loaded RAM against the disc maps module 0 to file offset 0x237dc, so the bundle's code is
//    WAD[0x237dc..0x35800]. Imported at its measured base (0x8006DA40) the analysis finds 98
//    functions, but the 19 distinct module bases are not function entries: the 8 KiB of module code
//    is `lui`/`addiu`/`sw` that COMPUTES POINTERS AND STORES THEM IN GLOBALS (module 0 opens `lui
//    $v0,0x8008; addiu $v1,$v0,-0x1CC4; lui $at,0x8007; sw $v1,0xB288($at)`), with ZERO `jal` in
//    the whole window. It is a setup table; the drawing it feeds is not in it.
//  * NO SPRITE TABLE IN THE EXECUTABLE EITHER: scanning SCUS_944.25's own file (0x800..EOF) for
//    record-shaped words gives 4 hits, all fixed geometry tables (0x8006016C, 0x800633BC,
//    0x800633D4, 0x80064848) and none of them a run.
//  * THE ATTRACT FLAG HAS NO STATIC REFERENCE: `refs 0x80066D40` over the analysed Spyro 2 program
//    returns 0 references, so the title state's own code cannot be reached from the one word that
//    is known to select it.
//
// SO: the emitter was not found, and this is where the search stops. The panel draws NO NAME rather
// than another title's artwork or a slice of the wrong one. What
// is still open is narrow and stated: the plate is drawn into the port's presentation without being
// a 15-bit RAM image and without the guest writing texture pages provat can see, so the next step
// is to find it in the populated texture memory as an INDEXED page (4- or 8-bit) with its CLUT —
// the one search not yet done — and name that page's origin, depth and CLUT here. The gate is
// already measured: `0x80066D40` is the guest's attract flag (clearing it takes the demo off the
// title screen; see spyro2_runtime.cpp).
inline constexpr spyro::TitleLogoFacts kLogoFacts{};

} // namespace spyro2
