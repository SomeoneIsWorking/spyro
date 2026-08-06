---
id: I040
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/ra_classes.py — audits every `jr $ra` psxport's ra_computed_jumps emitted as rec_dispatch instead of return, off the shipped generated/ set, in ~1s with no build

## Validated by

Run against BOTH classes by --selftest, which is wired to fail: a SYNTHETIC coroutine (jal then jr $ra on every path) must classify 'computed' and does — so Rule B can produce the positive answer and is not a rubber stamp; the REAL 0x800535E0 (bne at 0x8005358C skips the jal at 0x80053598) must classify not-computed and does; the REAL 0x80023A8C 'lw $ra,44(0x80077DD8)' must resolve its base and find the module-wide store at 0x80022A60, and does (19 stores). On the real executable it reports 9 of 778 jr-$ra sites classified computed and 9 of 9 proven ordinary returns, exit 1. Its negatives carry denominators (778 jr sites, 457 sw $ra sites, 790 emitted functions) and it REFUSES with exit 2 on a missing executable or a missing/empty generated/rec_decls.h rather than reporting 'no problems'. BLIND SPOTS it prints itself: MAIN only (overlay modules have their own partitions), Rule A cannot resolve a base that is not a local lui/addiu pair, neither rule sees a jr through a register other than $ra.


## Known failure modes

**FIXED 2026-08-06 — the zero-case refusal was a lie waiting to happen.** The script used to `die()`
with exit 2 whenever the emitter reported ZERO computed sites, on the stated grounds that it could
not tell "genuinely nothing to audit" from "a stale/absent generated set". That reasoning inverted the
moment the emitter was fixed and 0 became the TRUE answer for this game: a correct result was reported
as a refusal, so the tool could never confirm its own fix, and a real regression (a stale partition)
would have looked identical to success.

It can tell them apart, mechanically, and now does: `generated/.recomp_version` vs
`emit.RECOMP_VERSION` is the framework's own explicit staleness signal. The script REFUSES (exit 2)
when those disagree — "the partition on disk is not the one this emitter produces" — and otherwise
reports 0 as a PASS, printing its denominators (fragments, `jr $ra` sites, `sw $ra` sites) and its
blind spots rather than a bare "(none)". Verified BOTH ways on 2026-08-06: with `generated/` at
2026-08-05.1 and emit.py at 2026-08-06.1 it refused with exit 2 and named both versions; after the
re-emit it reported 0 of 778 with exit 0.

**Still a real blind spot:** the two rules are re-implementations, so the script agreeing with the
emitter is not independent evidence about the emitter's *code* — only about the classification. It
calls `emit.ra_computed_jumps` directly for the set it audits, so it always audits the LIVE analysis;
Rules A and B are the independent half.
