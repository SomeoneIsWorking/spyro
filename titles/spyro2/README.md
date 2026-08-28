# Spyro 2

Measured bring-up for `SCUS_944.25` (USA). The title-local generator validates the executable
against `executable.json` before emitting its ignored resident substrate. The exact 358400-byte
image produces 683 functions in nine translation units without foreign title seeds or guessed
overlays. A dedicated `spyro2_port` link keeps those generated symbols separate from Spyro 1's
different bodies at the same guest addresses.

`Spyro2Runtime` is a direct runtime: it binds no Spyro 1 `GameConfig`, hooks, context, renderer, or
generated objects. Its native boot owner reproduces the measured persistent stack frames of game
main `0x80011ADC` and boot prefix `0x80011E9C`, executes their constructor/first-leaf prefix, and
enters a finite display owner at `0x80011BBC`. The retained bootstrap has two direct libetc VSync
calls and clear helper `0x8004C484` has a third nested call. The product keeps both generated bodies
for A/B but dispatches neither: three host fields own their exact surrounding stack/register/memory
effects. DrawSync and its GPU timeout arm/check are synchronous title overrides with retained supers;
they do not consume a field, and the whole measured VSync body at `0x80058EDC` remains fatal.

Issue 0090's shared direct-runtime CD seam is resolved. Live PID `3564943` passed that shipping path,
completed all three host-owned display fields without guest VSync, and produced three visually
verified black clear frames before stopping at boot-prefix leaf `0x80011B1C`. Issue 0092 owns the
remaining post-display initialization and loader chain. No native renderer, widescreen, or temporal
interpolation claim exists for Spyro 2 yet.
