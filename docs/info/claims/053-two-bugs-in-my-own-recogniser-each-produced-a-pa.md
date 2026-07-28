---
id: C053
kind: claim
status: holds
created: 2026-07-28
tags: method
---

## Claim

Two bugs in my own recogniser each produced a PARTIAL match that looked like a clean no-match

## Evidence

First: the base is built , and I scanned BACKWARD, so the addiu was visited before the lui that gives it meaning and could never resolve. It still matched jr 0x8004C548 (whose operand order happened to suit it) while missing the neighbouring 0x8004C4E4 — a partial success that read as 'the idiom is rarer than I thought'. Second: after widening the window (needed because three consecutive dispatchers SHARE one  sitting ~68 instructions back), 'the last sll writing either operand' picked up an unrelated  on the BASE register, yielding idx==base and no constants — again a wrong match presenting as a clean None. Fixed by pinning the base register first and requiring the sll to write the OTHER addend, which is sound rather than heuristic: the add's two operands ARE the base and the scaled index.

## What would falsify it

A dispatcher where the base and index are the same register, which would break the 'other addend' rule.
