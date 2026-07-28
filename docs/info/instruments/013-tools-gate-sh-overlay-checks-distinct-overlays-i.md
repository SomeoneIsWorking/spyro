---
id: I013
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/gate.sh overlay checks — distinct overlays identified + arena loads unmatched

## Validated by

Both discriminate, checked across three logs from this session: pad_e.log (before the overlay set was complete) gives distinct=2 unmatched=3; ovl_all.log gives 7/0; the gate's own run gives 6/0. The check they replace grepped for the literal name 'OVL0' and reported 0 the moment overlays were renamed — it was testing a string, not the router, and it could not see an unmatched load at all (a run could identify one overlay, load three unknown ones, and still pass).

## Known failure modes

(none recorded yet)
