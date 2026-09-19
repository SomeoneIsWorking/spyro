---
id: 122
title: Oracle compare is structurally blind to interpolated frames, so it cannot verify 60fps
status: open
symptom: running the picture oracle with PSXPORT_FPS60=1 and without it produces byte-identical reports: same pixel counts at every checkpoint, same checkpoint frame counts, differing only in wall-clock seconds and the recorded product_env
tags: render,fps60,oracle,instrument,interpolation
created: 2026-09-19
updated: 2026-09-19
---

## Measured

Two picture-oracle runs, same binary, same route, same BIOS, back to back, differing only in
`--product-env PSXPORT_FPS60=1`:

| | save_picker | playing |
|---|---|---|
| fps60 off | 63768/122880 (51.89%), 452/480 tiles | 22730/122880 (18.50%), 225/480 tiles |
| fps60 on  | 63768/122880 (51.89%), 452/480 tiles | 22730/122880 (18.50%), 225/480 tiles |

Checkpoint frame counts are identical too (console 748 / native 687, console 1532 / native 2300).
Diffing the two `picture.json` reports, the ONLY differing keys are `seconds` (99.5 vs 100.5) and
`product_env`. The whole `pictures` block is byte-identical.

The feature was live, not silently refused. This run's own product log carries
`[fps60] TRUE per-object interpolated 60fps ON (source: env)` and
`[cfg] PSXPORT_FPS60 = true [env] (value=true)`. That refusal is a real branch -- `SpyroRuntime`
declares `temporalInterpolation = false` and only `Spyro1Runtime`'s `interpolatedNative()` override
turns it on -- so checking it was necessary and it passed.

## Why: the comparator samples exactly the frames interpolation does not touch

The picture oracle compares at logic-frame checkpoints. Interpolation adds presents BETWEEN logic
frames. So the two runs agree by construction, and will continue to agree no matter how wrong the
interpolated frames are. An identical report here is not evidence that interpolation is correct; it
is evidence that the instrument cannot see it.

## The deeper problem: there is no oracle for an interpolated frame

This is not a bug in the picture oracle that a better route would fix. The reference is a
full-console Beetle run of the retail game, which produces a picture per logic frame. A midpoint
frame reconstructed between two logic frames HAS NO COUNTERPART in the reference. A 30fps reference
cannot adjudicate a 60fps output; there is nothing to compare the extra frame against.

So "verify interpolated 60fps with oracle compare" cannot be satisfied as stated, for any route,
by either oracle. `tools/oracle_compare.py` reads guest RAM, which presentation does not touch;
`tools/picture_oracle.py` reads the picture at logic frames, which interpolation does not touch.
Both report zero difference and both are structurally blind.

## What CAN be checked, and what each would actually establish

1. **Non-invasiveness (already established, and this run extends it from RAM to picture).**
   Interpolation does not alter the logic-frame picture. Narrow, but it is now measured at the
   picture and not only in guest memory, which is a real step up from the S020 evidence recorded in
   project-state.
2. **Endpoint identity.** A midpoint reconstructed at t=0 must equal the previous logic frame's
   picture exactly, and at t=1 the next one. That is checkable against the product's own frames with
   no console reference, and it is a genuine falsifier: a transform, ordering or projection error
   shows up immediately as an endpoint that does not reproduce.
3. **Temporal consistency.** Between two logic frames, each tracked primitive's screen position
   should move monotonically and within the bound set by its own endpoints. A primitive that
   overshoots, reverses, or jumps to a wrong instance is a defect that needs no reference picture.
4. **Producer coverage with a denominator.** Which producers contribute interpolated geometry and
   which fall back to duplicating their endpoint. The existing counters report 1,216,422
   interpolated prims across 3,374 extra presents, but without a per-producer denominator that
   number cannot distinguish full coverage from one large producer carrying everything.

## What this does NOT say

It does not say interpolation is broken. Nothing here measured an interpolated frame at all -- that
is the whole point. It also does not say the picture oracle is faulty; it is doing what it was built
for, and its blindness here is a property of the reference, not a defect in the tool.

## Related

S020 in `docs/project-state.md` already says "the extra presents are never sampled by the
comparator". This issue is the measurement behind that sentence and the reason it cannot be fixed by
adding checkpoints. The 51.89% / 18.50% console differences in both runs are a separate matter and
are confounded by the two cores reaching each checkpoint hundreds of game frames apart -- issue 0119.
