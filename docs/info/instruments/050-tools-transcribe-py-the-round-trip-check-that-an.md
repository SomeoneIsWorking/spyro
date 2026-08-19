---
id: I050
kind: instrument
status: trusted
created: 2026-08-19
---

## Instrument

tools/transcribe.py — the round-trip check that an owned body emitted from the recompiled substrate still inverts to it exactly (transcribe.py check <addr> --body <inc>)

## Validated by

--selftest feeds the round-trip a faithful emission plus EIGHT corruptions (dropped statement, substituted register, delay-slot load made conditional, altered offset, altered mask, altered folded constant, altered GTE opcode, reordered statements) and requires the faithful one to PASS and all eight to FAIL — so it has shown both answers. Shown again on the real 5065-statement body: changing one offset (at+36 -> at+32) reported 'DIVERGES at generated statement 50 of 5065' naming both sides; restoring it returned OK. Wired into tools/gate.py (run_static_checks) as selftest-then-check.

## Known failure modes

(none recorded yet)
