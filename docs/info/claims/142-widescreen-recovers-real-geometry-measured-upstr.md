---
id: C142
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,measurement
---

## Claim

Widescreen recovers real geometry, measured upstream of the pixels: at 16:9 the world renderers emit 9-18% more packet bytes per call than at 4:3 (sky + distant terrain +18.4%, actor +11.6%, ground +9.3%). The gain is smaller than the 33.6% width increase because faces PARTLY on screen were never being rejected — only wholly-outside ones were. The measurement carries its own negative control: 0x80022A2C, which draws a foreground object and the screen-space DEMO MODE caption, moves +0.2% in the same runs, so the instrument is not simply reporting a difference everywhere.

## Evidence

PSXPORT_DEBUG=wideprims, two REPL runs to the same frame, compared at the DEEPEST CALL INDEX BOTH RUNS REACHED rather than at a frame number — call #N is the same call in a deterministic demo, whereas the runs traverse different amounts of the demo per wall-clock second (a 4:3 run to frame 30001 logged MORE calls than one to 46501, so run length was confounded). scratch/logs/wpfull_43.txt vs wpfull_169.txt. Bytes of packets emitted per call, 4:3 -> 16:9: 0x8004EBA8 12659 -> 14985 (+18.4%), 0x800258F0 17830 -> 19890 (+11.6%), 0x80020F34 2723 -> 2976 (+9.3%), 0x8001F798 7655 -> 7898 (+3.2%), 0x80022A2C 5398 -> 5410 (+0.2%). The uncontrolled reading gave the same figures to within 1 point.

## What would falsify it

a scene where a world renderer emits the SAME bytes at both aspects — that would mean the widened frustum is recovering nothing and the gain measured here is scene-specific
