---
id: I047
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/present_seq.py — reads a run of CONSECUTIVE present captures as a sequence: distinct colours, %pixels differing from the previous present, %differing from 2 presents back (the same display buffer), and a three-way GHOST-CANDIDATE test (pixels that differ from N-1 but equal N-2, i.e. content that reverted to an older buffer instead of being repainted), with a bounding box.

## Validated by

Run against BOTH classes on the SAME gameplay window (presents 4600..4623, moving-camera field): the persistence FIX reads 0/24 flat and 0 ghost px; the pre-fix build reads 15/24 flat and up to 93.3% ghost px with a full-screen bbox. It also has a built-in --selftest that synthesises a 3x3 revert and asserts the detector finds exactly it AND reports 0 on the no-ghost case (both legs, in the shipping file). It REFUSES (exit 3) with fewer than 3 readable captures or mismatched sizes rather than printing an empty negative, and prints its file/readable/unreadable denominators on every run. KNOWN LIMIT: a genuinely alternating element (the blinking PRESS START strip) is a real revert and is reported as a ghost candidate — the bbox is what separates it from a full-frame ghost.

## Known failure modes

(none recorded yet)
