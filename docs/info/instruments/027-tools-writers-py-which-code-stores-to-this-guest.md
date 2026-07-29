---
id: I027
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

tools/writers.py — 'which code stores to this guest address, and what VALUE does it store?'. Companion to xrefs.py. Prints the constant each store writes (from a preceding addiu/ori rt,zero,N), because with a global that has nineteen writers the bare address list is useless and the real question is always 'which writer produces the value the gate wants'. --value N filters to that directly.

## Validated by

Reproduces the ad-hoc scan it replaces exactly: 19 immediate-form writers of 0x80078D7C, and --value 5 isolates the single one (0x8007B8F8 in OV_5B800) that stores the value the issue 0027 gate requires. STATED BLIND SPOT, printed on every run rather than buried: it finds only lui/immediate-formed stores, so a store through a computed pointer is invisible — the exact failure mode that made two earlier static scans in this project miss the stage sub-state's writer, which a watchpoint then found in one run. It also lists writers that EXIST, not writers that RUN: in issue 0027 all 19 exist and a watchpoint showed none of them executed. Both limits mean an empty result is 'no immediate-form writer', never 'nothing writes this' — use PSXPORT_WWATCH for either question.

## Known failure modes

(none recorded yet)
