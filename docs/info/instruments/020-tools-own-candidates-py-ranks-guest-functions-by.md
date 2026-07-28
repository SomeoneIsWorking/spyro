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

## Known failure modes

(none recorded yet)
