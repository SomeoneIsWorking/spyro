---
id: I005
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/wad_index.py — WAD entry enumerator + code scorer

## Validated by

VALIDATED on a known case, and its FIRST VERSION WAS BROKEN in a way worth recording. v1 scored 'did the word decode as an opcode' and returned 100.0% for every entry including obvious data — uniform output, the classic broken-instrument tell. Cause: the decoder names unrecognised encodings op:0x2F / regimm:0x04 rather than reporting failure, so nothing ever counted as invalid. v2 scores the SHARE of words using opcodes that dominate real MIPS. It now discriminates and passes the ground-truth check: the already-extracted-and-recompiled overlay (entry 2, WAD +0x5B800) scores 99.5%, the highest of its neighbours, while entries 0 and 1 score 64.2% and 87.6%. CAVEAT: the 90% cutoff is MY choice, not derived; 36 entries clear it, which is suggestively close to the public decomps' ~37 overlays but must not be treated as confirmation of that number.

## Known failure modes

(none recorded yet)
