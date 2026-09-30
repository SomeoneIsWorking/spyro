---
id: I057
kind: instrument
status: trusted
created: 2026-09-30
---

## Instrument

`tools/route_trace.py` — per-delivered-field route trace and two-build comparison. `compare` reads
the `[pace]` line the product prints for every delivered field (field index `vbl`, the delivery
`site`, the title's 60 Hz `counter`) and the `[skipmap]` line it prints on every guest state change
(field number, load stage, gamestate, title mode/state/substate), both armed by
`tools/demo_run.py --debug pace,skipmap`. `ramdiff` compares two `PSXPORT_GRAMDUMP` captures word by
word. It answers "do two builds of the product behave the same on one route", which the route
corpus's mismatch COUNT cannot: that count is a function of how far into the game a run got before
its clock expired.

## Validated by

`tools/route_trace.py --selftest` (14 cases, registered as CTest `route_trace_selftest`): the
negative set is what matters, because this tool's output is a claim that two runs AGREE. A log with
no field lines, a field index that skips, a state line whose field number moves backwards and a
missing log are all REFUSED rather than read as an empty trace; a truncated second log is reported
as truncated rather than as agreement over the prefix; a size-mismatched or missing RAM capture is
refused rather than compared.

**Its other answer, on a real route.** The same binary run twice on the no-input attract demo
produces an identical field trace over the shared prefix and **byte-identical guest RAM — 0 of
524,288 words differ — at present 10,000** (`PSXPORT_GRAMDUMP`), so the instrument can report a
difference and its zero is a measurement rather than an absence. Used between `7d7f2eb` and
`a915b23` it found 0 differing words, identical traces over 35,734 delivered fields, and a whole-run
log diff of two log strings (docs/issues/0149).

## Known failure modes

* It compares the delivery sequence, the title's field counter and the guest state changes the
  product prints. It does not compare guest RAM, host state, or anything the product does not log;
  `ramdiff` is the separate instrument for the whole-memory claim.
* A run that ends early is TRUNCATED, not wrong, and the report says so — the trap this tool exists
  to close is a shorter run being read as a different one.
* `[pace]` and `[skipmap]` are the channels it reads. A build that stops printing either line makes
  `parse` refuse rather than return a short trace.
