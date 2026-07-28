---
id: I006
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/callsite_args.py — static argument-value recovery at every call site of a function

## Validated by

Reproduces ground truth: at the known site 0x80012924 it recovers a1=0x8007AA38, the OVL0 base independently observed from a running port (C031). Shows the OTHER answer: site 0x80012970 yields a different a1 (0x801BF800) and 5 of 11 sites report '?' rather than a value, so it is not returning a constant. FIRST VERSION WAS BROKEN and reported '?' for all 11 sites — the jal being analysed sat inside the simulated window and its caller-saved clobber wiped the very argument registers being reported. Caught by the uniform-output rule.

## Known failure modes

(none recorded yet)
