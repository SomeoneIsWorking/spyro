---
id: 119
title: The picture oracle compares two cores that are at the same checkpoint but not the same presented frame
status: open
symptom: tools/picture_oracle.py reports 51.89% differing pixels at save_picker and 18.50% at playing, and both are state offsets rather than rendering defects — the console reaches save_picker after 748 game frames and the product after 687
state_items: S019, S020
tags: oracle,picture,alignment
created: 2026-09-19
---

## What landed

`tools/picture_oracle.py` — the title entry point for psxport's `tools/oracle/picture.py`, sharing
title policy with `tools/oracle_spyro1.py` and the launch environment with `tools/drive.py`. It
compares the frame each core PRESENTS, which `tools/oracle_compare.py` structurally cannot: a
producer that draws nothing writes no different guest state, so a missing layer and a correct one
both report zero divergences.

The comparator is validated, not assumed: `--selftest` drives 60 frames of the title's own route and
measures 114,688 changed pixels, and a picture compared with itself differs in 0 — **it has shown
both answers.**

## What it found, and what that is NOT

Neither number below is a rendering defect. Both pictures are recognisably correct Spyro; the two
cores are simply not at the same moment.

| checkpoint | console | product | difference | what the pictures actually show |
|---|---|---|---|---|
| `save_picker` | 748 game frames | 687 | 51.89% | console has advanced to "CREATING SAVE FILE…"; the product still shows the three EMPTY slots with NEW GAME / LOAD GAME. Both are drawn correctly, including the frame chrome, the card-slot line and the world behind |
| `playing` | 1,532 | 2,300 | 18.50% | console is on the "THE ADVENTURE BEGINS…" title card; the product is 768 frames further on, into a near-black Artisans fade |

So the checkpoints hold the two cores at states whose DECLARED RAM RANGES are comparable, which is
all the RAM oracle needs, and not at the same presented frame, which is all a picture comparison
needs. That is the gap, and it is the reason S019 and S020 still say `partial` on same-state visual
comparison.

## Why this matters more than the numbers

On Tomba! 2 the RAM oracle reported 0 divergences over the exact route on which the user could see
the save menu was wrong. The picture oracle found the missing button glyphs on its first run, and
chasing them found an entire chrome producer that had never been installed — 16.5% of that title's
native override catalog turned out to be unreachable (Tomba2Engine issues 0013-0015). Spyro's RAM
route is equally clean and, until now, equally blind. This tool is how that blindness ends here, but
only once its comparisons are same-state.

## Next step: tighten the PREDICATE, not the frame count

The fix is title policy in `tools/oracle_spyro1.py`, not a rule in the shared comparator.

Equal frame counts would be the wrong criterion and must not be added: the product legitimately
reaches the same state in fewer frames than the console, because it does not wait on the same
loading. `native: game_stage after 27 game frames` against `console: ... after 302` is Tomba! 2
doing exactly that, correctly. Refusing on unequal counts would refuse every honest comparison.

What is actually wrong is that `save_picker`'s predicate fires across a range of sub-states — empty
slots, slot selected, creating the file — and any two of those look nothing alike. So:

1. Split the checkpoint into the sub-states a player can distinguish, each with a predicate that
   admits exactly one presented picture.
2. Where a checkpoint spans an animation or a fade by nature, it is not a picture checkpoint at all;
   mark it RAM-only rather than letting it report a percentage.
3. `--frame-step` inside a segment is the existing mechanism for comparing a sequence rather than a
   moment, and is the better tool for anything that moves.

Until then, treat this tool's per-checkpoint percentages on Spyro as unproven: the comparator is
validated, the alignment is not.
