---
id: I009
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/callgraph.py — direct-call reachability over resident text + optional overlay

## Validated by

Validated on two paths known to exist BEFORE any negative was trusted: 0x8003385C -> 0x80016500 (found, via the mode 4/5 arm 0x8002E12C) and 0x800144C8 -> 0x80016500 (found, adjacent). It also returns real negatives (0x80032B08 -> 0x80016500, 306 functions explored), so it discriminates rather than always answering one way. KNOWN BLIND SPOT, printed with every negative result: only DIRECT jal/j edges are followed, so function-pointer (jalr) calls are invisible — and Spyro leans on those heavily, including the very dispatch that matters (the mode-13 arm calls [0x800758CC] indirectly). Treat a found path as fact and a missing path as 'no direct path', never as impossibility.

## Known failure modes

(none recorded yet)
