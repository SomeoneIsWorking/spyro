---
id: C001
kind: claim
status: holds
created: 2026-07-28
tags: boot
---

## Claim

Spyro's crt0 (PS-EXE entry 0x8005B8E0) matches psxport's generic crt0_setup() field for field, so the boot group of GameConfig is fully derived, not guessed

## Evidence

Disassembly of 0x8005B8E0-0x8005B980: .bss clear loop 0x80075640..0x8007AA38; sp=[0x800755A8]-8; heapsz=(v0-[0x800755A4])-heapBase; sw heapsize->0x800730C4, heapbase->0x800730C0; gp=0x80075264; jal 0x8005DB14 (libcInit, a0=heapBase+4); jal 0x80012204 (main). Each maps 1:1 onto a crt0_setup statement.

## What would falsify it

if the port boots but guest globals read wrong early (bad heap/sp), one of these addresses is mis-assigned - re-derive from the disassembly
