---
id: C034
kind: claim
status: holds
created: 2026-07-28
tags: overlay
reconfirmed: 2026-07-28
---

## Claim

The level overlays' load base is NOT recoverable from their own bytes, and must NOT be assumed to be the arena

## Evidence

Two independent static routes both come back empty. (1) jal encodes its target's low 28 bits absolutely, independent of load address, so internal calls would reveal the base: entries 9/11/13/21/77 have ZERO jal targets above text_end 0x80075800 — every call goes back into resident text, so they contain no internal direct calls to triangulate from. (Verified OVL0 does have them, reaching 0x8007CD38 inside its own span.) (2) Embedded lui+addiu absolute constants above text_end cluster in .bss globals and stray to 0x801FFFFF, giving a ~1.6MB span for a 57KB module — no usable cluster. So the base is genuinely unknown for all 35. It would be easy and wrong to assume the arena 0x8007AA38 because that is where OVL0 lands; a wrong overlay base emits a whole module at wrong addresses, which is exactly the failure mode this port refuses to risk.

## What would falsify it

An observed loader call (PSXPORT_DEBUG=cdq logs a3 = the WAD byte offset alongside dest) whose a3 matches one of the odd entries — that single line settles the base for it. Reaching a level is what it takes.

## Re-confirmed 2026-07-28

STILL TRUE AS STATED, but read C039 before acting on it. C034's claim is that the base is not recoverable from the OVERLAYS' OWN BYTES, and that remains correct — they contain no internal direct calls and their embedded constants spread over ~1.6MB. What C039 shows is that the base is recoverable from somewhere else entirely: the RESIDENT text's handler-installer table at 0x8005A4BC writes 36 pointers spanning 0x80080548-0x8008B2C0, which is 67720 bytes above the arena base 0x8007AA38 and inside the level overlays' size range. So the practical warning in C034 — 'do NOT assume the arena' — is now superseded: the arena is where they load, on independent evidence. Keep C034's method lesson, drop its caution.
