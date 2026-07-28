---
id: I012
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_MISS_RAMDUMP — 2 MB guest RAM dumped automatically at every recomp-MISS (hle.cpp)

## Validated by

Its first use immediately contradicted the prevailing belief rather than confirming it: it showed the arena content at the miss matching the last-identified overlay for 0% of words, which is how the fourth overlay was found. An instrument that only ever agreed with the current theory would be the thing to distrust; this one falsified a recorded issue on first contact.

## Known failure modes

(none recorded yet)
