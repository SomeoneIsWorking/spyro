---
id: I039
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/codemap.py check — codemap drift gate, wired into tools/gate.sh as the 'codemap has no drift' check

## Validated by

Run against BOTH classes rather than reasoned about. POSITIVE (must fail): a map citing game/core/DOES_NOT_EXIST.cpp -> exit 1, names the stale path; a map mentioning only one file -> exit 1, names 2 uncovered subsystems; a nonexistent map file -> exit 1. NEGATIVE (must pass): docs/codemap.md -> exit 0, 'scanned 7 source subsystem(s) ... checked 49 referenced path(s) ... 0 coverage gap(s), 0 stale path(s)'. It now REFUSES with exit 2 rather than passing if no source roots are found, and prints its denominator plus its blind spots on EVERY run. WHAT IT CANNOT SEE, stated because it was green throughout the 2026-08-05 rot: a wrong STATUS, a cited claim since falsified, a resolved issue cited as live, a stale count, or two rows contradicting each other. On 2026-08-05 all 38 paths, 25 guest addresses and 23 symbols resolved while the map told a rendering agent to write a native producer to work around a fixed one-line framework regression (C099/C149). Treat green as 'no dangling references', never 'the map is true'.

## Known failure modes

(none recorded yet)
