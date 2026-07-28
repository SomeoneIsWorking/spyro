---
id: I007
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/gate.sh — the port's regression gate

## Validated by

DISTRUSTED ON ARRIVAL, recorded here because the failure is instructive rather than because the tool is now trusted. From its creation until this entry the gate reported PASS on a port that SEGFAULTS. Cause: it runs the port under 'timeout -s KILL', which swallows the child's exit status, and every check it made was a frame count or a log-line count that a crashed run still satisfies. The gate was written specifically to catch 'hollow' success and was itself hollow in exactly that way. Now checks the exit code first (a healthy run is one the gate had to kill, rc=137) and is correctly RED. Do not read its PASS on any pre-2026-07-28 result as evidence the port ran to completion.

## Known failure modes

(none recorded yet)
