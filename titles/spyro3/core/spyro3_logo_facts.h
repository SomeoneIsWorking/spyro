#pragma once

#include "title_logo_facts.h"

namespace spyro3 {

// NO VERIFIED LOGO FACTS YET. See spyro1_logo_facts.h for the form a filled header takes, and
// spyro2_logo_facts.h for why the Spyro 1 recipe does not simply transfer.
//
// MEASURED 2026-10-03 on SCUS_944.67, the same way Spyro 2 was measured (standalone boot, the whole
// machine saved from the control channel while it is running this title's own attract):
//
//  * The title screen comes and goes: it holds between the cards and the demo, so a boot that is
//    sampled every few hundred frames alternates the purple SPYRO / Year of the Dragon / PRESS
//    START screen with the demo (`scratch/s3seq/sheet.png`). A fact that gates on a frame number
//    would miss it as often as not; the gate has to be guest state, not a count.
//  * NO SPRITE TABLE: scanning the full 2 MiB of guest RAM for Spyro 1's eight-byte record shape
//    finds no run of six consecutive records and no cluster of records anywhere.
//  * The saved machine is `scratch/s3state/title.bin`; the frame it was taken on is the demo, which
//    is this title's other attract state and the same one its logo would have to be resident
//    beside.
//  * NOT A MOVIE, on the same measurements as Spyro 2: no `[fmv] begin` in a run that reaches this
//    title's title screen, no `[cd]` streaming after boot, and no `.STR` directory entry anywhere
//    in SCUS_944.67. The MDEC/STR route is not where this title screen comes from either.
//  * NOT EMPTY TEXTURE MEMORY: the saved GPU section is populated, and `provat` is not the
//  instrument
//    that can say otherwise — it reports `<never written>` for Spyro 1 too, where the logo's texels
//    are known to be resident.
//
// SO: the panel draws NO NAME for this title. What is still open is the same question Spyro 2
// raised — whether this title's wordmark is a guest texture at all, or host-drawn art, and if guest
// art, which rectangle of it.
inline constexpr spyro::TitleLogoFacts kLogoFacts{};

} // namespace spyro3
