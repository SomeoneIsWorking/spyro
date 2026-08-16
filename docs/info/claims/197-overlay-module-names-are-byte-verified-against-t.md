---
id: C197
kind: claim
status: holds
created: 2026-08-16
tags: overlays
depends: tools/overlay_scan.py#reference_names_for
---

## Claim

Overlay module names are byte-verified against the vendored decomp manifest

## Evidence

Hashing the retail scratch/wad/WAD.WAD slice at each overlay's wad_offset/length (game/overlays.json) against external/spyro-1/sha256sum.txt (the named .ovl builds) matches 6 of 12 runtime overlays: OV_5B800=titlescreen_code, OV_7F2800=level_0_artisans_home_code, OV_B83800=level_1_artisans_stone_hill_code, OV_237D000=level_10_peace_keepers_doctor_shemp_code, OV_2F5B000=level_15_magic_crafters_wizard_peak_code, OV_502F800=level_29_dream_weavers_icy_flight_code. Measured 2026-08-16. The decomp's 37 named overlays: 36 located byte-identically in the retail WAD index (35 levels + titlescreen); credits.ovl is not a sector-aligned WAD entry (the count reads 36). Annotated as reference_name in game/overlays.json by tools/overlay_scan.py --names; shown by tools/whatis.py; stable key remains OV_<hex>.

## What would falsify it

if a name in reference_name does not match the module a run actually loads at that arena offset, or if hashing a WAD slice at an entry's wad_offset/length no longer equals the sha256sum.txt value
