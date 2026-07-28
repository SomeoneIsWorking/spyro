---
id: C017
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

FALSIFIES C016: the cdc model never receives Spyro's CD commands

## Evidence

Counted the cdcw trace by register and bank across a full boot: ZERO writes to bank-0 register 0x1801, the only path into the model's command handler. All 44 writes are bank selects (w[1800]) plus bank-2/3 registers (volume / IRQ mask) — configuration, not commands. Consistently, PSXPORT_DEBUG=cdc logs nothing at all. The guest configures the controller directly but issues its real commands through libcd, which our CD_cw override intercepts before the model can see them.

## What would falsify it

if a run ever logs a bank-0 w[1801] write or any [cdc] command line, the model is receiving commands after all
