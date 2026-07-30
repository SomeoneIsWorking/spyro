---
id: I036
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

tools/shot.py capture-then-copy (scratch/screenshots/f<frame>.png)

## Validated by

CAUGHT LYING, now guarded. It always writes the SAME filename, so the usual idiom — run it, then copy f46501.png to a per-case name — hands back the PREVIOUS run's picture whenever a run fails to rewrite the file. That is not hypothetical: it mislabelled three of five mute captures (C138), which then read as a coherent finding ('these two renderers are one two-stage pipeline, their frames are byte-identical') and survived a day because byte-identical frames looked like a result instead of the same file twice. It was caught only because open-spyro's symbol names disagreed with the map. shot.py now records the output's mtime before the run and REFUSES to return a file the run did not rewrite. When capturing a series, still delete the output between runs — the guard reports the fault, it does not make copying safe.

## Known failure modes

(none recorded yet)
