---
id: C058
kind: claim
status: holds
created: 2026-07-28
tags: recomp,blocker
---

## Claim

The remaining fail-fast is a RETURN through a non-ra register, not a call — the recompiler dispatches it as if it were one

## Evidence

Chased with a new guest_find_word_to diagnostic added to the framework's miss report, which scans main RAM for the missed address: it found NONE, i.e. the value is not stored anywhere and is computed at the jump site. The C backtrace then named the chain: main -> gen_func_8003385C -> ov_ovl1_gen_8007DA78 -> gen_func_800385BC (so the LEVEL OVERLAY's code is executing, which is itself progress). c->pc pointed at 0x80053570, and gen_func_80053570's emitted body ends with 'rec_dispatch(c, c->r[7]); return;' — a . At runtime a3 holds 0x80038620, which is precisely the instruction AFTER 'jal 0x800530C0' at 0x80038618 in func_800385BC. So a3 carries a RETURN CONTINUATION and the  is a tail-return; the recompiler cannot tell it from a computed call and routes it to rec_dispatch, where a mid-function address is by definition not a function entry and fail-fasts. This is why no static reference exists (C056 was right about the symptom, wrong to suspect a data pointer table) and why no recogniser can help: the target is a runtime return address, not an enumerable case set.

## What would falsify it

a3 holding a value at that jr which is NOT the caller's return address — which would mean it is a genuine computed call after all.
