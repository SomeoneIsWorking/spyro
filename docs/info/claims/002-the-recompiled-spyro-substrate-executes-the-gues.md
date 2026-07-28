---
id: C002
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

The recompiled Spyro substrate executes the guest's own main(): the port reaches four call levels into recompiled game code

## Evidence

Headless boot backtrace: main -> gen_func_80012204 (crt0's jal target = guest main) -> gen_func_800127C0 -> gen_func_8001250C -> gen_func_80016500 -> gen_func_800163E4 -> Core::mem_w32. 621 functions emitted, entry 0x8005B8E0 recompiled.

## What would falsify it

if a later change makes the boot abort before gen_func_80012204 appears in a backtrace, this no longer holds
