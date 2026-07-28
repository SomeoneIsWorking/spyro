---
id: C016
kind: claim
status: falsified
created: 2026-07-28
tags: cd
falsified_on: 2026-07-28
---

## Claim

Spyro pokes the CD controller registers directly, so the framework's cdc model already sees its commands

## Evidence

PSXPORT_DEBUG=cdcw (upstream CD-register tracer) logs 44 register writes in a 25s boot: bank selects w[1800], param-FIFO pushes w[1802]/w[1803] and command writes w[1801], from pc=0x80065270 (ra=0x80063B18) and pc=0x80065108 (ra=0x80064000). One carries a0=0x800776D0. So the guest does NOT reach the controller only through the libcd functions we override — it drives 0x1F801800-3 itself, which mem.cpp routes into cdc_native.c.

## What would falsify it

if a run shows zero [cdcw] lines, the guest is not touching the registers and this is wrong

## FALSIFIED 2026-07-28

Falsified by C017, which was recorded at the time WITHOUT flipping this claim's status — so the ledger carried a known-dead claim as [holds] for the rest of the session. The [cdcw] writes C016 cited are real, but they are bank selects and param pushes that never carry a command through to the model; C017 measured zero bank-0 command writes reaching cdc_native.c. See C017 for the measurement.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
