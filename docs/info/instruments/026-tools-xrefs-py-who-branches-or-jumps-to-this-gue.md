---
id: I026
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

tools/xrefs.py — 'who branches or jumps to this guest address?', decoded NUMERICALLY from every 4-byte-aligned word rather than matched as text in a disassembly listing. Prints the DELAY SLOT of every hit, because the instruction after a branch runs either way and therefore decides the register state at the target. Works on the PS-EXE (base read from the header) and on raw overlay images (--base required, never guessed, since an overlay is keyed by its load address).

## Validated by

Three ways, because a scan that returns nothing looks identical to a broken scan. (1) Against the known-busy exit 0x8007CC48 it recovers all four branches a manual capstone listing showed, out of 100 total. (2) Against 0x8007CBA0 it finds the single predecessor at 0x8007CAA8 and its delay slot 'addiu v0,zero,5' — the fact that turned a supposedly-runtime question into the static answer 'the gate is s0 == 5' (C108). (3) On the PS-EXE with no --base it reports 11 branches to 0x80016500, matching callsite_args.py's independently-derived '11 static call sites' exactly. IT CAUGHT ITS OWN PREDECESSOR LYING: the ad-hoc string-matching version this replaced reported ZERO predecessors for 0x8007CBA0 — a false 'this code is dead' — because capstone disassembled linearly from an image base desyncs on data. KNOWN BLIND SPOT, stated in the tool's own no-hits message: it cannot see jr/jalr through a table or pointer, so 'nothing reaches it' means 'no branch encodes it', not 'unreachable'.

## Known failure modes

(none recorded yet)
