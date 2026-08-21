---
id: I020
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/own_candidates.py — ranks guest functions by how safe and worthwhile they are to own natively

## Validated by

Cross-checks against known ground truth: it reports the already-owned rand() 0x8006272C as leaf, 12 instructions, 41 callers, matching its disassembly exactly. Its top three picks were transcribed and all three matched the substrate byte-exact on 25 calls each, so the 'leaf + small + many callers' ranking selected genuinely transcribable functions. It exists because picking by eye failed: 0x8001ED5C looked like a small buffer flip and is actually the per-frame stage dispatcher — the tool's --addr mode reports it non-leaf and says why that disqualifies it.

The dependency-ready non-leaf mode has now shown both answers. Negative: `0x8003DF60` is an
approximate jr-ra split but not a generated MAIN entry, so `--addr` says NOT OVERRIDABLE and the
queue excludes it. Positive: the corrected <=120 queue selected `0x8005BE88`, FNTRACE reached it at
frame zero, and its native body matched the generated oracle exactly under NDIFF. Issue 0070 records
the defect and correction.

## Known failure modes

The jr-ra boundary scan is approximate. Before issue 0070, dependency-ready mode admitted three
mid-function return boundaries that the generated dispatcher and FNTRACE refused. It now intersects
with `generated/rec_decls.h`. The ownership denominator also used to inherit `--maxsize`, hiding the
large generated world owner; it now reports the complete `ndiff_run` source set at every queue size.
A stale generated tree would make the ownership queue stale, so run the normal recomp/build
provenance gate before landing an owner.
