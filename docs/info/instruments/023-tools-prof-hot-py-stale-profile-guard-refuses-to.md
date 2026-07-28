---
id: I023
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

tools/prof_hot.py stale-profile guard — refuses to resolve a profile against a binary relinked after it

## Validated by

Caught a real invalid comparison I was about to report. Re-resolving the pre-fix profile against the post-fix binary moved 'before' from 6.06% to 4.88% purely because symbol addresses shifted on relink — a difference that measures nothing. The guard now refuses with an explanation; verified it fires on the stale profile and stays silent on the current one. Same class as whatis.py's stale-RAM-dump guard (I015): an artifact that looks like data.

## Known failure modes

(none recorded yet)
