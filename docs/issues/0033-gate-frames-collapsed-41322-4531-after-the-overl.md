---
id: 33
title: Gate frames collapsed 41322 -> 4531 after the overlay set grew 7 -> 12
status: resolved
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

### Note (2026-07-29)
SETTLED BY MEASUREMENT, and BOTH of my earlier readings were wrong.

THE TEST: run the same binary for 20s and for 60s. If 4531 were a RATE, frames scale with wall time. They do not — 20s gives 4531 and 60s gives 4531, identical. So it is neither 'roughly nine times slower' (my first reading) nor 'correctly paced to the guest timebase' (my second). The port reaches frame 4531 and STOPS PRESENTING, while the process stays alive. A deterministic stall.

WHAT IT ACTUALLY IS — AND IT IS PROGRESS, NOT REGRESSION. With the watchdog enabled (the gate disables it, which is why this never showed):

  main -> 0x80012204 -> 0x8003385C -> 0x8004A200 -> 0x80048B9C -> 0x8004888C
       -> 0x8003DAE4 -> ndiff_run

0x8003DAE4 calls 0x80016CB0 and 0x80016C58 — the angle-table bodies this port owns — so ndiff_run in the trace is just an owned callee, not the problem. This is ordinary game code looping without completing a frame, on the LEVEL-LOAD path (0x8003385C is one of the level-load probe sites).

So the old 41322-frame runs were not 'faster'. They never left the TITLE SCREEN, whose attract loop presents frames indefinitely. Now that issue 0027 is fixed the port advances into level loading and stalls there, presenting far fewer frames in the same wall time. A frame count is a proxy for throughput ONLY while the port is doing the same work; across a change that alters how far it gets, comparing frame counts compares different things. That is the real lesson here and it invalidates the premise this issue was filed on.

RETITLE in spirit: this is not a throughput regression, it is a NEW STALL during level load, reachable only since 0027 was fixed. Next step is to find what 0x8003DAE4's caller chain is looping on — it is gated on [0x80078B08] < 23, which looks like an object/entity count, so start by watching that and 0x80078B74.

THE GATE DEFECT IS REAL AND UNCHANGED, and is now better motivated: 'frames >= 300' passed a run that stopped presenting entirely after 4531 frames. It should assert frames still ADVANCING near the end of the window, not merely a total — a stalled run and a healthy one are indistinguishable to a cumulative floor.

### Resolution (2026-07-29)
RESOLVED — it was the frame-4531 stall (issue 0034), not a slowdown, and the reframe note had already reached that conclusion: a number repeating to the digit across rebuilds is a deterministic bound, not a machine running slower.

Root cause was a recompiler bug — jalr treated as a block terminator, dropping a function's epilogue, so a loop counter came back holding a stack pointer and ran ~62 million times. Fixed in psxport; the port went from a hard stop at 4531 to 69360 frames in the same 60s. Overlays.json growing 7 -> 12 was a coincidence of timing: it made the level-load path REACHABLE, which is when the pre-existing bug started biting.

THE GATE DEFECT THIS ENTRY IDENTIFIED IS ALSO FIXED, and it was the more valuable half. The gate ran with PSXPORT_WATCHDOG=0, so a port that stopped presenting entirely still passed every check — 'frames >= 300' counts frames from before the stall. It now runs the frame-progress watchdog at 15s and FAILs on a trip, printing the top of the stuck stack. Validated both ways: silent on a healthy run, and its pattern matches the log from this very stall.

The relative-throughput check this entry suggested is NOT implemented, deliberately: gate frame counts vary with machine load (27k-49k across this session's runs on an unchanged build), so a relative floor would fire on noise. The watchdog catches the failure mode that actually matters — no progress at all — without that false-positive surface.
