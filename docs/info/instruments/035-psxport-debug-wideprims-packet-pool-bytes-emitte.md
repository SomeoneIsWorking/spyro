---
id: I035
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

PSXPORT_DEBUG=wideprims — packet-pool bytes emitted per renderer call (game/core/wide_clip.cpp)

## Validated by

VALIDATED, and by the discriminator rule rather than by looking right: in the SAME pair of runs it reports +18.4% for one renderer and +0.2% for another, so it is not uniformly reporting a difference, and it was run against BOTH classes (4:3 and 16:9) from the same counter. It also reports zero-emit calls explicitly with both pool pointers, so 'drew nothing' cannot read as 'was never called'. KNOWN LIMIT: the 'other aspect' figure printed on each line is only populated if the aspect is toggled LIVE within one run; across two runs that column reads 0 and the comparison must be done by joining the two logs at a matching CALL INDEX (not a frame number — run length is confounded, see C142). Replaces the 4:3-vs-16:9 pixel correlation that could not discriminate (I034).

## Known failure modes

(none recorded yet)
