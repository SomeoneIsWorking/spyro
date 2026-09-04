# Spyro 3

Measured bring-up boundary for `SCUS_944.67` (USA). The independently supplied 380,928-byte
executable matches all 11 identity-manifest facts, and the shipping crt0 scanner resolves all eight
boot fields plus the heap plan. `Spyro3Runtime` owns that image without binding Spyro 1 config,
hooks, context, or overrides.

Binary analysis of the identified executable found 639 resident function entries, including crt0
`0x80059444` and game main `0x8001200C`, without title seeds or Spyro 1 overlay input. No Spyro 3
target executes it yet. The measured libetc VSync entry is `0x8005956C`; its helper
`0x800596E4` owns the timeout path and references the executable's `VSync: timeout` string at
`0x8001165C`. A future native-owned frame loop must bind `0x8005956C` as a fatal guest-call trap,
never a success HLE.

No Spyro 3 CHD was available, so disc provenance remains unverified. Lightrec execution, boot
comparison, native producer, widescreen support, and interpolation support are also unverified.
Title-specific work remains behind Spyro 1 completion.
