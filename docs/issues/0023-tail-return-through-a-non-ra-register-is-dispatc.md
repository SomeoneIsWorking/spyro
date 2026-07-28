---
id: 23
title: Tail-return through a non-ra register is dispatched as a call
status: open
symptom: [recomp-MISS] at a mid-function address with no static reference and stored nowhere in RAM — e.g. 0x80038620, the instruction after a jal, reached via jr $a3.
tags: recomp,framework
created: 2026-07-28
updated: 2026-07-28
---

See C058 and docs/issues/0021's resolution section.

gen_func_80053570 ends with rec_dispatch(c, c->r[7]) — i.e. `jr $a3` — where a3 holds a RETURN
CONTINUATION rather than a call target. The recompiler cannot tell a tail-return from a computed call,
so it dispatches, and the target (being mid-function) is not a function entry and fail-fasts.

PREFERRED FIX (emit-time, decidable, cannot mask a real miss): if the register feeding a terminal `jr`
was loaded from `ra` anywhere in the function — directly (move a3,ra) or via the stack (sw ra,N(sp)
then lw a3,N(sp)) — emit `return` instead of rec_dispatch. The C call stack then unwinds to the real
caller, which resumes after its own call site: precisely the guest semantics.

ALTERNATIVE (runtime, broader): rec_dispatch treats an address strictly inside a recompiled function as
a tail-return and returns. Simpler, but it would mask a genuine mid-function call — though such a call
is fatal today regardless.

Worth measuring how widespread the pattern is before choosing: count terminal `jr rX` (rX != ra) across
the resident text and see how many have an ra-derived source.
