---
id: I018
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_REPL=1 — interactive inspection of a live Spyro port (now pumped from the frame boundary)

## Validated by

Driven end-to-end on real input: 'run 600' then 'r 0x800785E8' returned B0 7B 18 80 B0 3B 1A 80 ... — the same OT/pool pointers the snapshot file holds, from an independent path. 'press start' reported held=FFF7 and 'run 120' advanced the port to frame 720. Previously unreachable in this port for a STRUCTURAL reason, not a broken one: repl.read() is pumped only from the framework's native scheduler loop, which never runs while the guest owns its frame loop.

## Known failure modes

(none recorded yet)
