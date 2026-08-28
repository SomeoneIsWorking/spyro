# Spyro 3

Measured bring-up boundary for `SCUS_944.67` (USA). The independently supplied 380,928-byte
executable matches all 11 identity-manifest facts, and the shipping crt0 scanner resolves all eight
boot fields plus the heap plan. `Spyro3Runtime` owns that image without binding Spyro 1 config,
hooks, context, or overrides.

A binary-only recompiler pass over the identified executable discovers 639 resident functions,
including crt0 `0x80059444` and game main `0x8001200C`, without title seeds or Spyro 1 overlay input.
That generated set remains an ignored bring-up artifact rather than a shipping substrate: no
Spyro 3 target installs or boots it yet. The measured libetc VSync entry is `0x8005956C`; its helper
`0x800596E4` owns the timeout path and references the executable's `VSync: timeout` string at
`0x8001165C`. A future native-owned frame loop must bind `0x8005956C` as a fatal guest-call trap,
never a success HLE.

No Spyro 3 CHD was available, so disc provenance remains unverified. Boot comparison, native
producer, widescreen support, and interpolation support are also unverified.
