---
id: C035
kind: claim
status: falsified
created: 2026-07-28
tags: pad,input
falsified_on: 2026-07-28
---

## Claim

Spyro's game code NEVER touches the pad: zero JOY-register accesses and zero pad-library calls in the resident text and in all 36 code overlays

## Evidence

Three independent scans, all exhaustive rather than sampled. (1) JOY registers 0x1F801040-0x105F, matching lui+load/store and lui+addiu/ori base forms: 0 hits in resident text (against 1028 references to the rest of the I/O page, so the scan is finding I/O access in general) and 0 across all 36 code overlays. (2) The libapi pad chain is LINKED BUT DEAD — InitPAD 0x80068D10, StartPAD 0x80068D20, StopPAD 0x80068D30 and PAD_init 0x80068D40 each have exactly ONE caller, the libapi wrapper that forwards to them, and the head of that chain (0x80068ABC / 0x80068A2C) has no callers and is never address-taken anywhere in the text. No overlay calls any of them either. (3) No code anywhere jumps directly to a BIOS vector 0xA0/0xB0/0xC0 bypassing a trampoline, so the trampoline census is complete. CONSEQUENCE: input cannot be produced by running more guest code — it has to be delivered by the BIOS/HLE layer, and GameConfig's pad group (padSlot0Buf/padSlot1Buf/padDriverFn/padSlotPtrTable) is still all zero, so the framework is currently delivering nothing.

## What would falsify it

Any JOY-register access or pad-library call found in code this scan did not cover — the resident text and the 36 code entries are covered, so the gap would be a 37th overlay, code loaded from a non-code-scored WAD entry, or self-modifying/computed dispatch.

## FALSIFIED 2026-07-28

The scan behind it only recognised addresses built with lui/addiu at the access site; Spyro reaches SIO0 through a pointer variable ([0x80075220] = 0x1F801040), so the instrument could not have returned any other answer. See C064.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
