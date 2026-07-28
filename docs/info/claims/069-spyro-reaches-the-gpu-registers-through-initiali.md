---
id: C069
kind: claim
status: holds
created: 2026-07-28
tags: gpu,instrument
---

## Claim

Spyro reaches the GPU registers through initialised POINTERS, not immediate addressing — so an immediate-address scan reports zero GPU accesses, exactly as it did for SIO (C064).

## Evidence

Scanning every lui+simm pair for 0x1F801810/0x1F801814 across the whole text yields ZERO hits. But the image holds those addresses as DATA: [0x80074B34]=0x1F801810, [0x80074B38]=0x1F801814, [0x80074B3C]=0x1F8010A0 (a libgpu register block), plus [0x800738BC]=0x1F801814 (the GPU status mirror vsync.cpp already relies on) and four more words holding 0x1F8010F0. Same shape as the libpad SIO base at [0x80075220]=0x1F801040.

## What would falsify it

finding any lui/addiu-built access to 0x1F8018xx in the text, which would mean the pointer indirection is not universal
