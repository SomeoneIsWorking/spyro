---
id: I031
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

fntrace's ABI check — every traced call snapshots the callee-saved registers (s0-s7, gp, sp, fp, ra), runs the body, and compares. A guest compiler never returns with these changed, so a mismatch means that function's RECOMPILATION is wrong. It is the only cheap way to see that class of bug, because the damage lands in the CALLER's locals and surfaces far away (typically as a loop that will not terminate). Reports the first violation per site plus a total count.

## Validated by

Validated by forcing the other answer, via PSXPORT_FNTRACE_SELFTEST=1, which XORs s0 after the call so the check MUST fire: it reports '0x8001F798 VIOLATES THE ABI: s0 entered as 8006FCF4, returned as 25A35951' with 1856 violations over 1856 calls. Without that self-test a '0 violations' result would be indistinguishable from a checker that cannot report at all — and a passing control case proves nothing, since a broken checker passes everything. First real use REFUTED the hypothesis it was built for: 0x8004888C and 0x8003DAE4 report 0 violations across 62,331,761 calls each.

## Known failure modes

(none recorded yet)
