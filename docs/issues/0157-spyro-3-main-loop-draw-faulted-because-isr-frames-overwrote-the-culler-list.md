---
id: 157
title: Spyro 3's first main-loop draw faulted at 0x1f800400 because guest interrupt handlers ran on the interrupted $sp and overwrote the culler's list
status: resolved
symptom: Lightrec execution fault invalid load/store at address PC 0x1f800400, guest pc 0x8001C4EC, ra=0x1F8003EB, preceded by intr timeout(0004:000d)
tags: spyro3,lightrec,interrupt,exception-stack,scratchpad,cd
created: 2026-10-01
updated: 2026-10-01
---

## Answer

**Scratchpad execution is NOT involved.** The fault PC printed by Lightrec's `__segfault_cb` is a load/store
ADDRESS, not an instruction address. The faulting instruction is the GTE word `0x4A180001` (RTPS) at
`0x8001C4EC` after `add $t5,$t5,$t6` with `$t6 = 0x1F800000`: the vertex pointer was a scratchpad address
one byte past the 1 KiB scratchpad (`0x1F800400`), so the list that supplied it was corrupt.

Root cause: psxport delivered guest interrupt handlers (the libetc dispatcher chain at `0x8006A2CC`, DMA
callbacks, the CD ready callback) on the INTERRUPTED `$sp`. Spyro 3's culler runs with `$sp` repurposed as a
data pointer into its list, so the handlers' stack frames were written over that list while the culler was
suspended. Per-step logging showed the list valid at step 116 and overwritten at steps 337-338. The real
BIOS switches `$sp` to a kernel exception stack before walking the chain (SCPH1001.BIN `bfc108c8/bfc108d0`).

Fix (framework, psxport `96462c8e`, psxport issue 0144): `Hle::enterExceptionStack` points `$sp` at
`kExceptionStackTop` before the chain walk, the DMA callbacks and the CD ready callback. psxport tests fail
without it (ctest 193/193 with it).

## Result

`tools/boot_run.py --title spyro3 --frames 1500 --fallback-limit 100000000`: before, NAMED STOP at
`0x8001C4EC`, 573 fields, 363 steps. After: PRODUCT-STEP CAP, 1,500 fields, 913 steps, 913 fences,
2,306 translated blocks, 31,331,827 executed blocks, 181,278,555 instructions, 0 faults, 0 fallback,
13/13 CD deliveries. Spyro 2 report is byte-identical to before (784 steps, 8,460,612 blocks, 0 faults).
Spyro 1 `tools/drive.py gameplay` reaches GS_Playing (frame 6380).

## Open: `intr timeout(0004:000d)` and the unregistered CDROM handler

The message is still printed once. It comes from the guest's own wait loop for a CDROM interrupt. The native
`cdInitSuccess` replaces retail `CdInit` (`0x8005DB1C`) and skips its handshake at `0x8005DBAC`, so the guest
CDROM interrupt handler is never registered in the libetc table. Not verified: that `0x80061B9C` is not the
registrar, what this gap breaks later, and the real BIOS stack value at `0x6cf0`. Next frontier for S025.
