---
id: I029
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_NO_NATIVE=1 (game/core/game_hooks.cpp) — install NO natively-owned bodies, so every call runs the recompiled substrate. The A/B switch for 'is one of our own replacements responsible?'. It answers that on paths the per-call differential CANNOT reach: PSXPORT_NDIFF verifies the FIRST N calls of each site, so a body that is wrong only after millions of calls — on inputs a later level produces and the title screen never does — is invisible to it. Probes and platform supply are deliberately not gated, since removing those changes what the port can do at all and would confound the comparison.

## Validated by

Made to answer a live question and gave a falsifiable answer immediately: the frame-4532 infinite loop (issue 0034) reproduces at exactly last-frame 4531 both with and without native bodies, exonerating all 18 owned bodies. The switch is only trustworthy because it changes something observable — a normal run and a NO_NATIVE run differ in which code executes (the substrate bodies run instead), and both were confirmed to reach the same frame rather than one silently failing earlier.

## Known failure modes

(none recorded yet)
