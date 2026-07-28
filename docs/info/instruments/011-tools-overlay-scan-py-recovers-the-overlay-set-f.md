---
id: I011
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/overlay_scan.py — recovers the overlay set from a run's cdq arena loads into game/overlays.json

## Validated by

Shown to report the OTHER answer rather than a fixed list: a first scan of an older log found 5 overlays, a fresh 45s run found 7 including the two the port only reaches now that input works (+0x2F5B000, +0x502F800). It also correctly returns nothing and SAYS SO (non-zero exit, explicit message) when handed a log captured without PSXPORT_DEBUG=cdq — tested on scratch/logs/ovl3.log, which was run with the ovload channel only. So 'no overlays' is distinguishable from 'the tool saw nothing'.

## Known failure modes

(none recorded yet)
