# Spyro 2

Measured bring-up for `SCUS_944.25` (USA). The exact 358400-byte executable is authenticated by
`executable.json`; binary analysis identified 683 resident function entries without foreign-title or
guessed-overlay assumptions. The retired emitted-code bring-up kept this image separate from Spyro 1,
proving that identical guest addresses cannot select title behavior by address alone. The target
runtime instead keys Lightrec cache and overrides by complete image identity plus address.

`Spyro2Runtime` is a direct runtime: it binds no Spyro 1 `GameConfig`, hooks, context, renderer, or
title behavior. Its native boot owner reproduces the measured persistent stack frames of game
main `0x80011ADC` and boot prefix `0x80011E9C`, executes their constructor/first-leaf prefix, and
enters a finite display owner at `0x80011BBC`. The retained bootstrap has two direct libetc VSync
calls and clear helper `0x8004C484` has a third nested call. Three host fields own their exact
surrounding stack/register/memory effects. DrawSync and its GPU timeout arm/check are synchronous title overrides;
they do not consume a field, and the whole measured VSync body at `0x80058EDC` remains fatal.

Issue 0090's shared direct-runtime CD seam is resolved. Live PID `3564943` passed that shipping path,
completed all three host-owned display fields without guest VSync, and produced three visually
verified black clear frames before stopping at boot-prefix leaf `0x80011B1C`. Issue 0092 owns the
remaining post-display initialization and loader chain. No Lightrec gameplay, native renderer,
widescreen, or temporal interpolation claim exists for Spyro 2 yet. Title-specific work resumes only
after Spyro 1 passes its migration gate.
