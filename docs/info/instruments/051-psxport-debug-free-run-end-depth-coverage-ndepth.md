---
id: I051
kind: instrument
status: trusted
created: 2026-08-19
---

## Instrument

PSXPORT_DEBUG-free run-end depth coverage: '[ndepth] depth coverage (<why>): N of M prim(s) carried REAL per-vertex depth = X% 3D' plus the stale/absent miss split and the buffer-to-buffer carry counts (framework render_depth_coverage_report, called from game/core/producer_run.cpp finish_once)

## Validated by

It has shown BOTH answers on the same binary within one session: 2.10% before the ProjPrim word guard and 63.60% after, over the identical 6100-frame reference-leg recipe, and 70.53% with the guard compiled out — three different numbers from three different mechanisms, so it is not a constant. It also states the ZERO case as prose ('NO PRIMITIVES WERE CLASSIFIED AT ALL this run — not 0% 3D, but nothing measured'), which is the exact failure that got I041 distrusted. Counters are lifetime totals, never reset, and every figure is printed with the denominator it is a fraction of.

## Known failure modes

(none recorded yet)
