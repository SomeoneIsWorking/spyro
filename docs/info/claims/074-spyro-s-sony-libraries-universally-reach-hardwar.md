---
id: C074
kind: claim
status: holds
created: 2026-07-28
tags: instrument,hardware
---

## Claim

Spyro's Sony libraries UNIVERSALLY reach hardware through initialised pointer tables, never immediate addressing — so an immediate-address scan is useless for finding hardware access in this game, and 'zero hits' from one means nothing.

## Evidence

Three independent subsystems, each with ZERO lui/addiu-built accesses across the whole text and each with a pointer table in initialised data: SIO0 [0x80075220]=0x1F801040 (libpad, C064); GPU [0x80074B34..3C]=0x1F801810/14/0x1F8010A0 plus [0x800738BC]=0x1F801814 (libgpu, C069); CD [0x800750FC..08]=0x1F801800/01/02/03 with DMA3 at [0x80075138..40]=0x1F8010B0/B4/B8 (libcd). The pattern is the SCE libraries' own idiom, not a game choice, so expect it for libspu/libmdec too.

## What would falsify it

finding any lui/addiu-built hardware-register access in the text would mean the rule is not universal and each subsystem must be checked separately
