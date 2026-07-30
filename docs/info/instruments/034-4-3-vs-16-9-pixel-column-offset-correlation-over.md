---
id: I034
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

4:3-vs-16:9 pixel/column offset correlation over a gameplay capture

## Validated by

NOT VALIDATED — CAUGHT LYING, and it is the reason wide_clip.cpp carries its own instrument. Asked 'by how much did widescreen shift the frame', a summed-squared-error offset search over a VARIABLE-width overlap returned its own SEARCH BOUND (+240) as the best match in all three bands, because a larger offset compares fewer columns and scores better for free. Re-run with a FIXED 400-column window it put the designed +86 in the top cluster but could not separate it from a spurious +179 — the DEMO MODE scene is mostly flat sand and has too little horizontal structure to discriminate. WHAT TO USE INSTEAD: (a) correlate two captures of the SAME aspect differing only in the change under test, which did discriminate (sky +85, ground +77 against a designed +86); or better (b) measure UPSTREAM of the pixels — PSXPORT_DEBUG=wideprims reports bytes of packets emitted per renderer per call, and reports in BOTH aspects so the two are comparable (game/core/wide_clip.cpp).

## Known failure modes

(none recorded yet)
