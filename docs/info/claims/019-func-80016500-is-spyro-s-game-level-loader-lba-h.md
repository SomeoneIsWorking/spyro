---
id: C019
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

func_80016500 is Spyro's game-level loader (lba/handle, dest, len) and serving it moves real data

## Evidence

Probing up the call chain logged a0=0x25 (37 = WAD.WAD's LBA), a1=0x8007AA38 then 0x801BF800 (distinct destinations), a2=0x800 then 0x40000 — the (offset, destination, length) shape. Owning it (read sectors -> dest, then super-call) now moves real bytes: 2048, 262144, 14336, 110592 across successive calls with VARYING lengths, i.e. the guest is making progressively different requests.

## What would falsify it

if the loaded bytes are later shown not to match the disc content at those offsets, or a0 turns out to be a handle rather than an LBA (it is always 37 despite varying lengths, which is suspicious), the mapping is wrong
