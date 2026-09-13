---
id: I005
kind: instrument
status: trusted
created: 2026-09-14
---

## Instrument

console PC-store and RAM-range observation (external/psxport/tools/oracle/console.py observe; driven by scratch/oracle-comparison/*_console.py): counts guest stores at named PCs and snapshots chosen guest RAM words at every hit, per display field

## Validated by

Showed both answers. Positive: the shaded-pass byte bracket (scratch/oracle-comparison/shaded_pass_bytes_console.py) advanced D_800757B0 by 5068/5156/5040/4932/5000/5268 bytes on six consecutive rendered fields while several accepted records showed a zero delta (ordinal 103) and only 8 of the 9 accepted world records moved it — a uniform nonzero result would have been the tell that the pointer was not per-record. The handoff cadence capture scanned 13,032,200 stores at 48 retained hits with zero drops and zero pairing errors, and the per-field stage/level/switch words matched the loader sequence. Cross-checked in-process: the console's accepted record count (9 world commits per rendered field) equals the oracle's shaded_flagged=9, by identity of the three class-83 nodes at list ordinals 0, 1, 2.

## Known failure modes

The observer sees only stores at the PCs it was given and only the RAM ranges it was given — a
register value (the record pointer, for instance) is not observable, so per-record attribution has to
come from list order plus a separately measured ordinal. The record ring holds 128 records and is
drained once per stepped field: a field that fires more than that drops records and the driver raises
instead of reporting a partial count, so a target with a high hit rate needs its own capture.

Choosing the wrong word as "the cursor" produces uniform zero deltas that look like "the code wrote
nothing". In the shaded pass `$gp+0x3E0/0x3E4/0x3E8` (0x80075644/48/4C) are constants while
`D_800757B0` is the pointer the pass advances; and `D_800757B0` is committed once per accepted record,
so its per-record deltas measure that record's own bytes only if the snapshot is taken at that record's
entry.

Console fields are not the native build's frame counter: the same level state sat about 77 fields
apart in the two runs used here, and the level's opening camera pan makes geometry phase-sensitive, so
a comparison must match phase explicitly rather than assume the same index.
