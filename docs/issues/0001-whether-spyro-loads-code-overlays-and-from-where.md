---
id: 1
title: Whether Spyro loads code overlays — and from where — is UNRESOLVED
status: open
symptom: The recomp covers only the resident executable (621 fns from SCUS_942.28). If the game loads code overlays, that code is NOT recompiled and will fail fast as [recomp-MISS] once reached.
tags: recomp,overlays,provisioning
created: 2026-07-28
updated: 2026-07-28
---

## Why this is open

**Evidence FOR overlays existing:** public decomp projects (theMagicalKarp/open-spyro, TheMobyCollective/spyro-1) describe the game as the main EXE **plus 37 overlays**.

**Evidence AGAINST them being separate disc files:** `discdump list` over Spyro the Dragon (USA) returns ONLY:
SYSTEM.CNF, SCUS_942.28, WAD.WAD, SOURCE/SOURCE.TRD, S0/* (the bundled Crash demo), PETEXA*.STR.
There are no per-overlay files. So if 37 overlays exist they live **inside WAD.WAD** (110 MB) or are read by raw LBA.

A WebFetch summary asserted the overlays are individual disc files. That contradicts the disc itself and was NOT trusted — it reads as the summarizing model inferring from a source tree layout.

## What was tried and did not work

An ad-hoc scan for direct jal/j targets outside the resident text, intended to reveal overlay entry addresses. **Unusable** — see the distrusted instrument entry. The .data tail of the text image decodes as spurious jals, swamping any real signal.

## Why it matters

It decides whether `tools/ensure_recomp.py` must grow a WAD.WAD extraction + overlay-recompile step, and whether `game/recomp_seeds.json` needs `overlay_bases`/`overlay_base_patterns`.

## How to actually settle it

Run the port until it fails, and read the failure: the substrate fail-fasts with `[recomp-MISS] 0x800xxxxx` on any call it cannot resolve. A miss to a consistent address region ABOVE the resident text is the overlay load slot, and `PSXPORT_DEBUG=cd` logs the load destination directly — the same way psxport's reference consumer captured its overlay bases. That is ground truth from a running system, unlike a static scan.
