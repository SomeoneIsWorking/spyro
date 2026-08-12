---
id: I048
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/verify_producers.py

## Validated by

BOTH CLASSES, in the shipping path, every gate run. POSITIVE: the real files agree — shipped kGuestSpriteEmitter 0x8007CD38 == the address DERIVED from OV_5B800's bytes (unique lui $r,0x0900 POLY_FT4-tag site at 0x8007CD64, 1 of 3584 words, walked back to the enclosing jr-ra-delimited prologue; 56 jal sites in the image target it). NEGATIVES, all six caught with the message asserted so none can pass for the wrong reason: shipped constant off by 8 -> DISAGREE; constant renamed away -> REFUSE; fingerprint word blanked in a copy of the image -> REFUSE 'matched 0 site(s)'; fingerprint duplicated -> REFUSE 'matched 2 site(s)'; run report with 0 rows -> DISAGREE; row with prims_native=0 -> DISAGREE. LIVE SABOTAGE, not just mutants: (a) the real constant hand-edited to 0x8007CEE4 (the arm) -> rc=1 DISAGREE, restored, git diff clean; (b) the ProducerScope narrowed to '{ ... }' so it closes before the push -> static half stayed GREEN and the --db half went rc=1 'reported NO row keyed 0x8007cd38' while the run itself warned '1376 native prim(s) drew with NO ProducerScope open'. BLIND SPOTS: it verifies the KEY, not that the transcription is faithful; the fingerprint is per-producer, so a new ProducerScope with no recipe is a REFUSAL rather than a pass; it needs scratch/bin/overlays/*.BIN (a build) and REFUSES with exit 2 if absent rather than reporting nothing found.

## Known failure modes

(none recorded yet)
