---
id: C008
kind: claim
status: holds
created: 2026-07-28
tags: boot
---

## Claim

The post-splash stall is a SPIN, not slow initialisation

## Evidence

Sampled the mem_w32 address argument under gdb while the guest ran: values repeat at 0x801FFDB0 and 0x801FFDB4 — adjacent slots just below the stack top 0x801FFFF0, i.e. the same frame being re-pushed. Combined with a 4-sample stack profile pinned to func_800163E4 <- 80016500 <- 8001250C <- 800127C0 <- main, and the process still alive and in the same chain 28s in.

## What would falsify it

if the write addresses are later seen advancing over a wide range, or the guest leaves that call chain on its own, it is doing real work and this is wrong
