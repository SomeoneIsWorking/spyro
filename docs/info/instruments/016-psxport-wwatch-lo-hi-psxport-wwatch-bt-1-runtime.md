---
id: I016
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_WWATCH=<lo>,<hi> (+PSXPORT_WWATCH_BT=1) — runtime write-watchpoint on a guest address range

## Validated by

Answered 'who writes [0x80078D78]' in one run after TWO static scans had failed to — the store goes through a register, invisible to lui/addiu-immediate scanning. It reports the writing pc, the ra, the frame, and the full register set. It also DISPROVED a belief rather than confirming one: it showed continuous activity at a state I had called hung. Reach for this before a third static scan for a writer.

## Known failure modes

(none recorded yet)
