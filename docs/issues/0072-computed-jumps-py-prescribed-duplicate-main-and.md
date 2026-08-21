---
id: 72
title: computed_jumps.py prescribed duplicate main and main_reentry ownership after emitter semantics changed
status: resolved
symptom: The current computed-jump locator ends every run by telling maintainers to put a mid-function entry in BOTH main and main_reentry, although main_reentry is now itself a resident discovery root.
tags: recomp,tooling,seeds,main_reentry
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

The tool's locator remained useful, but its final prose had copied the 2026-07-28 emitter contract
into executable guidance. At that time `main_reentry` only supplied fallthrough-boundary metadata,
so an interior entry also had to be put in `main` to enter discovery. Current psxport instead unions
`main_reentry` directly into the resident discovery roots and passes that same set as boundary
metadata. The framework changed authority; the consumer tool's prose and historical C050 claim did
not move with it.

## What was tried / dead ends

Changing the sentence to recommend `main_reentry` alone for every address would still be wrong for
this tool's primary output. Its computed-offset cases are labels inside existing functions, and the
locator's case-count heuristic is known to over-report. Those addresses belong in the recompiler's
recognizer after hand verification, not in either per-game seed list.

## Resolution

`tools/computed_jumps.py` now separates the two cases explicitly: heuristic computed-offset
candidates are recognizer inputs and must not be seeded; a separately measured true mid-function
re-entry goes in `main_reentry` only. The emitted guidance is one function used by the real CLI and
a four-case `--selftest`, registered as the normal `computed_jumps_selftest` CTest. The seed-file
schema note, codemap, I010, and historical issue 0020 now state the same current contract. C050 is
marked falsified rather than silently rewriting the old experiment.
