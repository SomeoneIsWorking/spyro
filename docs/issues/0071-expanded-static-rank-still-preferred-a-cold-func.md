---
id: 71
title: Expanded static rank still preferred a cold function over the reached SPU reset
status: resolved
symptom: The next dependency-ready non-leaf appeared to be 0x800181AC by static caller count, but a real 3,000-field run never called it while lower-ranked 0x8005BBF4 ran at frame zero.
tags: ownership,reach,fntrace,ndiff
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

`tools/own_candidates.py` ranks static opportunity: caller count is a lower-bound call-graph property,
not evidence that the current executable route reaches a body. The queue was working as specified,
but treating its first row as the next implementation target conflated potential coverage with live
coverage. In the measured 3,000-field boot, the 55-caller 0x800181AC and the smaller 0x80017FE4 were
cold while the lower-ranked PsyQ init at 0x8005BBF4 ran during frame-zero boot.

## What was tried / dead ends

Expanding the size limit alone did not solve target selection: it merely exposed more statically
dependency-ready bodies, still ordered without runtime reach. Implementing the top row before tracing
would have produced another native body that the shipping gate could not exercise or differentially
verify.

## Resolution

Run one mixed FNTRACE batch before implementation, with a known-live child in the same capture so an
all-zero trace cannot masquerade as evidence. `scratch/logs/gate-boot-20260821-035503.log` reached
0x8005BBF4 once at frame 0 from ra=0x8005BA9C and reached positive-control 0x8005BE88 once, while
0x80017FE4 and 0x800181AC were explicitly NEVER CALLED. The reached 165-instruction InitSpuHardware
body was then owned bottom-up and matched its retained generated body on the real call (C211). Future
non-leaf selection remains: dependency filter first, FNTRACE reach second, implementation and NDIFF
only after both pass.
