---
id: C013
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The gate is cleared by the guest's CD event callback func_80016490(2), never invoked because no IRQ is raised

## Evidence

func_80016490 has NO direct jal callers; its address is only BUILT as a value and handed to libcd's callback registration (func_8006623C) at 0x800124A8 and 0x80016420 — the signature of an interrupt handler. Its a0==2 path clears the gate 0x80076BB8 plus 0x800758CC/0x800758E0. Delivering that event from our synchronous CD path clears the gate (logged: 'delivered CD completion -> gate now 0'), the wait in func_80016500 then succeeds, and the guest issues its next request.

## What would falsify it

if a run shows the gate cleared without func_80016490(2) having run, something else clears it and this is not the mechanism
