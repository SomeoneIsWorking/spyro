---
id: I051
kind: instrument
status: distrusted
created: 2026-08-19
---

## Instrument

PSXPORT_DEBUG-free run-end depth coverage: '[ndepth] depth coverage (<why>): N of M prim(s) carried REAL per-vertex depth = X% 3D' plus the stale/absent miss split and the buffer-to-buffer carry counts (framework render_depth_coverage_report, called from game/core/producer_run.cpp finish_once)

## Validated by

It has shown BOTH answers on the same binary within one session: 2.10% before the ProjPrim word guard and 63.60% after, over the identical 6100-frame reference-leg recipe, and 70.53% with the guard compiled out — three different numbers from three different mechanisms, so it is not a constant. It also states the ZERO case as prose ('NO PRIMITIVES WERE CLASSIFIED AT ALL this run — not 0% 3D, but nothing measured'), which is the exact failure that got I041 distrusted. Counters are lifetime totals, never reset, and every figure is printed with the denominator it is a fraction of.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-09-19 — its call site no longer exists, so it can never print

The instrument is `render_depth_coverage_report`, which this record says was called from
`game/core/producer_run.cpp` `finish_once`. **That file no longer exists and nothing in this
repository calls the function.** Searched every tree under `~/repo/psx` excluding build/, .git/ and
vendored checkouts: the only references are the framework's own declaration and definition
(`psxport/runtime/psx/render_stats.h`, `gpu_native.cpp`), Spider-Man's registry, and this file.

So every figure this record quotes — 2.10%, 63.60%, 70.53% — describes a build whose call site has
since been removed. They are history, not a current measurement, and no run of this port can produce
a new one until a call site is restored on the native producer path.

`tools/depth_cov.py` is dead here for a different reason and must not be used as a substitute: it
parses the per-frame `[ndepth fN]` lines emitted from the framework's `gpu_native.cpp`, which is the
GUEST-OT compositor. This port's product render path is `native` (PC producers -> SDL_GPU), which
never reaches that code, so the tool reports "59 sampled frames, 0 carrying primitives" — measured
2026-09-19 over a `drive.py gameplay` route that had reached Artisans. That is the "instrument never
ran" case wearing the clothes of "scanned and found none", and the tool does not distinguish them.

Consequence: **claim C143's falsifier is currently unmeasurable.** C143 says psxport's widescreen 2D
widen cannot be enabled here because 2D-vs-3D discrimination rides on per-primitive depth at ~2.5%
coverage, and names rising depth coverage as what would falsify it. Nothing can report that number
today, so C143 can be neither confirmed nor falsified until an instrument is restored. C143 is also
about the guest-OT compositor's widen, which the native render path does not use; whether Spyro's
2D content widens is now a render-queue 2D-space question (`RQ_2D_AUTHORED_4_3` vs
`RQ_2D_WIDE_FINAL`), the same mechanism as Tomba! 2's issue 0010.

## STILL DISTRUSTED 2026-09-19 after the call site WAS restored — the counters are on the wrong path

`spyro::reportRuntimeRun` (`game/core/runtime_run.cpp`) now calls
`render_depth_coverage_report(&core, "run-complete")`, so the instrument prints again. It prints:

```
[ndepth:warn] depth coverage (run-complete): NO PRIMITIVES WERE CLASSIFIED AT ALL this run
              — not 0% 3D, but nothing measured.
```

on a run that had reached Artisans and presented 3,606 frames. That is the honest negative, and it
settles the question the previous section left open: **restoring the call site is not enough, and no
call site can fix this.** The counters this function reports are incremented inside the framework's
guest-OT classifier in `gpu_native.cpp`, which a native-producer run never executes, so they are
zero wherever the report is placed.

The call site is kept anyway, because a report that states "nothing measured" in prose is strictly
better than silence, and its own comment now says why. But do NOT read its output as a coverage
figure for this port, and do not treat a future non-zero from it as native-path coverage without
first checking which classifier incremented it.

What would actually measure native-path depth coverage is a count taken where this port's producers
set it — at `RqItem::depth` on submission — not in the compositor they bypass. Nothing does that
today, so the number remains unavailable and C143's falsifier remains unmeasurable.
