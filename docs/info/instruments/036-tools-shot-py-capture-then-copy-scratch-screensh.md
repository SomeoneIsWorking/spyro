---
id: I036
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

tools/shot.py capture-then-copy (scratch/screenshots/f<frame>.png)

## Validated by

CAUGHT LYING, now guarded. It always writes the SAME filename, so the usual idiom — run it, then copy f46501.png to a per-case name — hands back the PREVIOUS run's picture whenever a run fails to rewrite the file. That is not hypothetical: it mislabelled three of five mute captures (C138), which then read as a coherent finding ('these two renderers are one two-stage pipeline, their frames are byte-identical') and survived a day because byte-identical frames looked like a result instead of the same file twice. It was caught only because open-spyro's symbol names disagreed with the map. shot.py now records the output's mtime before the run and REFUSES to return a file the run did not rewrite. When capturing a series, still delete the output between runs — the guard reports the fault, it does not make copying safe.

## Known failure modes

### `scratch/screenshots/` IS A SHARED ACCUMULATOR — NEVER GLOB IT (recorded 2026-08-06)

The guard added above catches ONE run failing to rewrite ONE file. It does not touch the larger
hazard, which is the directory itself: **`scratch/screenshots/` is written by every run in this repo,
by every tool, from every session, and nothing ever clears it.** Files from unrelated runs days apart
sit side by side with no run identity on them, so any `scratch/screenshots/*.ppm` /
`sorted(glob(...))[-1]` / "the newest one" idiom silently mixes runs.

MEASURED CONSEQUENCE, on the sibling port (spider1, 2026-08-05): a glob over this shared accumulator
swept a STALE leftover file into an analysis as "the correct reference frame". The comparison against
it then produced **an entire false root cause for Spider-Man's flicker** — a widescreen explanation
that was subsequently REFUTED. Nothing about the run looked wrong; the analysis was internally
consistent and simply about the wrong file. This repo has the same failure in its own history at one
level lower: C138, three of five captures mislabelled by the same-filename idiom.

Why it is so hard to catch: a stale capture is a REAL, VALID picture of a REAL run. Every sanity check
you would apply — is it a plausible frame, has it the right dimensions, is it non-black, does it have
a sensible colour count — passes. There is no signal of wrongness in the artifact at all.

**THE RULE THAT PREVENTS IT, and it is two halves — the first alone is not enough:**

1. **Write to a PER-RUN DIRECTORY**, named for the run (`scratch/screenshots/<run-id>/…`), created
   fresh. Never write into the shared root and never read a series out of it.
2. **VERIFY EVERY FILE AGAINST ITS OWN CAPTURE LOG LINE BEFORE READING IT** — the run's own
   `present_shot` / capture line names the path and the present index it wrote. A file that no log
   line in THIS run's log claims is not this run's file, whatever directory it is in and whatever its
   mtime says. mtime is not proof: a run that crashes before capturing leaves the previous file
   newest, which is exactly the C138 shape.

A per-run directory without step 2 still fails whenever a run dies before writing what you expected
and you read the file a previous invocation left in that same directory.
