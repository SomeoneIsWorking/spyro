---
id: I030
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

PSXPORT_WWATCH's reported 'pc' is the last function ENTERED, not the storing instruction — and it goes STALE after a call returns. The generated dispatcher sets c->pc on function entry and nothing restores it on return, so any store executed in a caller AFTER a call is attributed to the callee. Use tools/writers.py (or the ra, which is genuine) to find the real store site; treat pc as 'the last function entered before this store', nothing more.

## Validated by

Caught by a contradiction rather than by inspection, which is why it is worth recording. A watchpoint attributed 6.3M stores to pc=0x80016AB4 (ratan2), but writers.py found no store to those addresses anywhere inside ratan2 — the real sites are 0x8003DCF0/DD04/DD14/DD28, in the CALLER's body immediately after two ratan2 calls, exactly where a stale pc would mislead. The accompanying ra=0x8003DC20 was correct throughout and pointed at the right place. Any conclusion in this project that named a writing FUNCTION from a wwatch pc, rather than from writers.py or ra, should be re-checked.

## Known failure modes

(none recorded yet)
