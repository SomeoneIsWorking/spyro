# Spyro 2

Measured bring-up boundary for `SCUS_944.25` (USA). The executable matches the identity manifest,
and `Spyro2Runtime` inherits the lineage runtime while owning its own typed crt0 image. It binds no
Spyro 1 `GameConfig`, hooks, context, overrides, or generated substrate.

No Spyro 2 CHD was available in this measurement, so disc provenance remains unverified. There is
also no generated substrate, boot comparison, native producer, widescreen support, or interpolation
claim. `bootInit` refuses at that exact boundary instead of entering guessed code.
