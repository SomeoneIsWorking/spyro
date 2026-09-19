---
id: 125
title: Guest kernel page 0 differs from the console in ten of its first 128 bytes
status: open
symptom: a declared range over 0x80000000..0x8000007F is unequal to the Beetle console reference at all 14 oracle checkpoints, 10/128 bytes differing, native 00 against console 03 at +0
state_items: S019
tags: oracle,bios,hle,fidelity
created: 2026-09-19
---

## The observation

Added while chasing issue 0123, because a card probe was found writing 128 bytes over this exact
page. `tools/oracle_compare.py --bios ../SCPH1001.BIN` with `kernel_page0` declared:

```
kernel_page0 (informational): first diff at +0 native 00 console 03, 10/128 bytes differ
```

0 of 14 checkpoints equal. The same 10 bytes, with the same first-diff values, in every
configuration tested -- including with and without the card write that prompted the look. So the
card probe was never the cause; the page was already unfaithful and nothing was watching it.

## Why it is informational rather than decisive

No title behaviour has been traced to these bytes. Every decisive range is equal to the console at
every checkpoint (182/182), so whatever lives here is not currently moving anything the oracle
declares as mattering. Marking it decisive would fail the gate on a divergence with no known
consequence; leaving it undeclared is how it stayed invisible.

## What is open

Identify what writes 0x80000000..0x8000007F on each side. On hardware this page holds BIOS kernel
data below the exception vectors, so the likely answer is an HLE boot step this runtime performs
differently or not at all. Ten specific bytes is a small enough target to name exactly.

Until then this range is the tripwire: if a future divergence appears here, the count changing from
10 is the signal.
