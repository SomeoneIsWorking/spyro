---
id: 1
title: Whether Spyro loads code overlays — and from where — is UNRESOLVED
status: resolved
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

### Note (2026-07-28)
ANSWERED (pending one decode check): the guest loads CODE from WAD.WAD into the HEAP and calls it. Evidence: recomp-MISS at 0x8007ABAC = heapBase+0x174, inside the 2048 bytes the owned loader read from WAD.WAD at LBA 37; caller ra=0x800339E4. This is why the disc file tree showed no per-overlay files — the overlays are inside WAD.WAD, exactly as the public decomp projects' '37 overlays' implied. Consequence for the port: those overlays are NOT in the recompiled set (emit.py only saw SCUS_942.28), so every call into them will fail fast. Recompiling them needs their bytes extracted from WAD.WAD plus their load base, which is now known to be the heap.

### Note (2026-07-28)
RETRACTED — the previous note on this issue was WRONG. It claimed the overlay question was answered because a recomp-MISS landed at heapBase+0x174 inside loader-written bytes. Checking that claim's own falsifier disproved it: the words there (00256000 037F2800 0000D000 037FF800) ALL decode as sll, while every real function entry in this executable opens with addiu or lui. Four consecutive sll is DATA. So 0x8007ABAC is not loaded code — the guest is calling through a garbage pointer, most plausibly read out of bytes MY loader wrote incorrectly. A later load shows the same region as all zeros, so the content is not even stable. THIS ISSUE IS OPEN AGAIN: whether Spyro loads code overlays, and from where, remains unresolved. The recomp-MISS is now a lead on a LOADER BUG (wrong bytes at the destination), not evidence of overlays.

### Note (2026-07-28)
REFINEMENT of the retraction. Two separate inferences were bundled and only ONE holds:\n\n  HOLDS: 0x8007ABAC is not loaded CODE. Its words all decode as sll; real entries open addiu/lui. The 'Spyro loads code from WAD and calls it' claim (C024) stays falsified, and this issue stays OPEN.\n\n  DOES NOT HOLD: 'therefore my loader wrote wrong bytes'. Those same words are ALL exactly 0x800-aligned (1196 / 28645 / 26 / 28671 sectors) — the shape of a WAD sector-offset index, which is exactly what the archive's first sector should contain. Four words landing sector-aligned by chance is ~(1/2048)^4. So the loaded content is structurally VALID (C025), and the loader-corruption lead is weakened, not confirmed.\n\nWhat remains genuinely unknown: where the garbage pointer 0x8007ABAC came from. It is not from these words being wrong; it is more likely the guest indexing this table with something we have not yet got right, or reading a pointer the port never populated.

### Resolution (2026-07-28)
ANSWERED on static evidence: Spyro calls code that must be loaded above the resident image. The instruction at 0x800339DC is 0x0C01EAEB — a DIRECT jal to 0x8007ABAC — and three siblings hardcode calls to 0x8007AA50, 0x8007BFD0 and 0x8007CEE4, all from genuine low game code, all clustered just above text end (0x80075800) around heapBase (0x8007AA38). A hardcoded call into non-resident space means the game statically expects code loaded there, which is the overlay mechanism the disc file tree could not show (no per-overlay files; they live in WAD.WAD). Consistent with the public decomps' 37 overlays. NOTE the path here: an earlier runtime-sampling answer was retracted, then the retraction refined, then the original conclusion re-confirmed from the BINARY. Sampling that region showed data because it sampled before the code was loaded there — a timing-dependent observation could never settle a question the instruction encoding answers outright. CONSEQUENCE: those overlays are not in the recompiled set (emit.py saw only SCUS_942.28), so each call fails fast. Recompiling them needs their bytes out of WAD.WAD plus their load base, which these four targets bracket.
