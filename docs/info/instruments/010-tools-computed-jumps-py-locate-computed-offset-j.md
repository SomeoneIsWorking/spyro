---
id: I010
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/computed_jumps.py — locate computed-offset jumps (jr base+idx*stride)

## Validated by

PARTIALLY TRUSTED — good for LOCATING, not for seeding, and the output says so. Validated positively: it finds the known failing site, reporting base=0x8004C4EC stride=16, which matches the hand-decode (C049) and the six case addresses that demonstrably advanced the port. KNOWN DEFECTS, measured not guessed: (1) the case-count stop heuristic never fires — it reports the max-cases limit (24) for three of four runs, so run tails are over-reported; (2) it produced one clearly spurious detection (stride=2, base 0x8004C550); (3) its backward walk found the jr at 0x8004C548 rather than the actual dispatching jr at 0x8004C4E4, so the reported jr address is approximate. Seeding its raw output (73 addresses) would carve up real code. Use it to locate and hand-verify recognizer inputs, never as a seed generator. The current emitter makes `main_reentry` alone a resident discovery root plus fallthrough boundary; the tool's 4-case selftest requires that guidance and rejects restoring duplicate `main` authority.

## Known failure modes

Before the post-9f1 audit, the final line fossilized the old emitter mechanism and prescribed putting
one interior PC in both `main` and `main_reentry`. Current psxport discovers the entry directly from
`main_reentry`; following the old output would create duplicate authority even though the locator's
computed-offset candidates should not be seeded at all (issue 0072, C050 falsified).
