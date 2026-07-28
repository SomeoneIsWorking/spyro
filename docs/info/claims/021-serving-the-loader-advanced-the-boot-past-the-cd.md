---
id: C021
kind: claim
status: holds
created: 2026-07-28
tags: boot
---

## Claim

Serving the loader advanced the boot past the CD wait; the stall moved to func_8005CBB0

## Evidence

5-sample profile after the loader fix: 5/5 samples in gen_func_8005CBB0, called from gen_func_80014564 <- gen_func_800127C0 <- gen_func_80012204 (main). That is a DIFFERENT branch from the CD wait (func_80016500) that held the boot for the previous several iterations — func_80014564 was previously only a reader of the CD gate. So the CD work did unblock the path it was blocking.

## What would falsify it

if a later profile shows the guest back in func_80016500's wait, the CD path regressed and this no longer holds
