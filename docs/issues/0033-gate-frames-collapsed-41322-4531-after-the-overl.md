---
id: 33
title: Gate frames collapsed 41322 -> 4531 after the overlay set grew 7 -> 12
status: open
symptom: Same 40s gate, same thresholds, all 14 checks still PASS, but frames presented fell from 41322 to 4531 (~9x), bytes loaded from disc 27.8M -> 3.7M, CD loader invocations 14 -> 6, distinct overlays identified 7 -> 4. The port is not failing, it is running roughly nine times slower and therefore getting far less far in the same wall time.
tags: perf,overlay,gate,blocker
created: 2026-07-29
updated: 2026-07-29
---

TIMELINE. The only change between the 41322-frame gate and the 4531-frame gate is game/overlays.json growing 7 -> 12 overlays (issue 0032) plus the ensure_recomp.py re-emit and rebuild. The EvMdINTR fix gated at 41322 immediately before, so it is not implicated. The hot native body dl_link was already installed at 41322 too.

NOT YET EXPLAINED. A host profile in the gate's own configuration (PSXPORT_NDIFF=8) puts the top costs at 28% outside the binary, 11.8% MultiplyMatrixByVector, 10.4% ndiff_run, 9.7% mem_w32, 3.3% the ndiff state accessor. Nothing there is obviously 9x worse than before, and the profile shape differs from the pre-expansion one in ways that may just reflect the port being in a different state.

THE GATE DID NOT CATCH THIS, and that is its own defect. 'frames presented >= 300' passes a run that lost 90% of its throughput. The CLAUDE.md already records the matching story (a CD change took the port from 18809 frames to 8) and the lesson was to watch the per-check numbers, not the PASS. A gate that cannot distinguish 41322 from 4531 is not protecting throughput at all — consider a relative check against a recorded baseline rather than an absolute floor.

FIRST STEPS: (1) confirm causality by reverting overlays.json to the 7-entry set, re-emitting and re-gating — if frames return to ~41k the overlay expansion is confirmed as the cause rather than a coincidence. (2) If confirmed, look at what per-dispatch work scales with the overlay COUNT: the router resolves which overlay is resident for an arena address, and going 7 -> 12 candidates makes any per-call scan proportionally worse.

### Note (2026-07-29)
REFRAME — 'roughly nine times slower' is probably the WRONG reading, and the tell is exactness. Three consecutive gate runs, across two different builds (before and after a psxport rebase that pulled in an upstream CD pump), present EXACTLY 4531 frames. The pre-change runs varied normally: 41410, 41282, 41322. A number that repeats to the digit across rebuilds is not a machine running slower — that would jitter — it is a deterministic bound.

So the question is not 'what got slow' but 'what is now pacing the run'. The likeliest candidate is the vsync change made in the same window: vblank_wait used to advance its own counter (cur++) and write it back, and it now RE-READS [0x800749E0] because the guest's VBlank root handler owns that increment (C118). If the root handler advances the counter at a different rate than one per wait iteration — or does not advance it on some path — the wait loop's progress per unit of work changes, and the frame count becomes a function of the guest's timebase rather than of host speed. 4531 frames over a 40s gate is ~113/s, which is suspiciously close to a plausible paced rate rather than a free-running one.

That would make this NOT a regression at all but the port becoming correctly paced, with the previous ~1030 frames/s being the port racing ahead of the guest's own vblank counter. Do not assume either way: MEASURE the relationship between [0x800749E0] and presented frames before concluding.

CONCRETE NEXT STEP, cheap: watchpoint [0x800749E0] over a fixed run and compare its increments against the presented-frame count, once with the root-handler path active and once forced onto the fallback (captured slot-4 handler). If the counter now advances once per presented frame where it used to advance faster, this issue is resolved as 'correct pacing' rather than fixed.

The GATE DEFECT stands regardless of which way that lands: 'frames >= 300' cannot distinguish 41322 from 4531, and a relative check against a recorded baseline would have flagged the change for inspection instead of passing it silently.
