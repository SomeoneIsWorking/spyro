---
id: I019
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_NDIFF=<n> — per-call differential between a native body and the recompiled one (native_diff.cpp)

## Validated by

Validated in BOTH directions, which is the only way this instrument means anything. (1) It reports a difference when one exists: changing the LCG addend from 12345 to 12346 produced 'RAM 0x80075AC0: native=1F substrate=1E' on call #1 — a single byte. (2) It found a REAL inequivalence I had not noticed: the recompiled rand() body's final 'lui at,0x8007' leaves 0x80070000 in $at, which my native version did not reproduce; it matched for 9 calls and then diverged the moment a caller left a different value there. (3) After fixing that, 200 consecutive calls matched exactly across RAM, scratchpad, all GPRs and hi/lo. Note the limit when citing it: equality means equal for the inputs exercised, and it cannot see state the substrate keeps outside guest RAM.

## Known failure modes

(none recorded yet)
