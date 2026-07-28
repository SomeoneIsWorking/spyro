---
id: I003
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

single gdb stack sample

## Validated by

NOT a profile — DISTRUST for 'where does time go' questions. One sample says where the process was once. Used alone it produced a confident wrong conclusion twice this session (once attributing a crash to guest code that was actually in the present path, once nearly discarding a real hot path). Take >=4 samples, or don't claim a hot path. The 6-sample version IS trustworthy: it agreed 6/6 before a fix and 0/4 after, i.e. it demonstrably shows the OTHER answer when the other answer is true.

## Known failure modes

(none recorded yet)
