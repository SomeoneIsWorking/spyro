---
id: 73
title: Nested NDIFF corrupts its parent snapshot when an owned body calls another owned body
status: resolved
symptom: The long ownership gate reports four RAM-byte differences for spu-init@0x8005BBF4 at 0x801FFEF8..FB even though the nested spu-pio child and the same parent previously matched.
tags: ndiff,framework,ownership,spu,oracle
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

`ndiff_run` documents and tracks nested comparison windows with `s_in_diff`, but the actual snapshots are singleton vectors and register/GTE structs in one process-global `State`. When the native `InitSpuHardware` parent calls the separately owned `WriteSpuRamPio` child, the child `ndiff_run` overwrites the parent's `pre`, native, substrate, and register snapshots. The parent substrate leg is therefore not rewound to the parent entry state.

The four reported bytes are one little-endian word: native `94 BD 05 80` = `0x8005BD94`, the executable-derived return address that both parent implementations set before calling `0x8005BE88`. Address `0x801FFEF8` is the nested child's saved-RA stack slot (`sp -= 48; [sp+40] = ra`). This exact address/value relationship names snapshot nesting as the cause; it is not an unexplained body-output mismatch.

## Evidence

Framework `d2ff7887b06e3f763aa550e915a554881ce9700c`, Clang build, `PSXPORT_NDIFF=8` 9,000-field reference gate: `scratch/logs/gate-boot-20260821-134100.log`. `spu-pio@0x8005BE88` calls 1 and 2 match, then parent `spu-init@0x8005BBF4` reports only RAM `0x801FFEF8..0x801FFEFB` as `94 BD 05 80` vs zero. The same run later reports `text-sprites@0x800181AC` calls 1 through 8 match.

## Proper fix and falsifier

Make the shipping NDIFF implementation keep a snapshot frame per active `ndiff_run` (or otherwise make its full comparison state reentrant), without disabling child or parent checks. Add a focused upstream nested native/body test through `ndiff_run`: positive nested-equivalent parent+child must produce zero divergences; an independently mutated parent or child must produce the opposite answer. After that framework change lands, rebuild this port and require the real SPU parent and child to both match in the same `PSXPORT_NDIFF` run. No game-side body change is justified by the current evidence.

## Resolution

psxport `3418a79b624765614f3f198dc1e89632e1e650f0` retains one reusable complete snapshot frame per active NDIFF depth. Its shipping-API test passes the equivalent nested parent/child with zero divergences and independently catches a mutated child twice while the parent remains matched (2/2 tests, 8 checks). Rebuilt against that exact commit, Spyro's real 9,000-field reference gate (`scratch/logs/gate-boot-20260821-141206.log`) reports both `spu-pio@0x8005BE88` child calls and the `spu-init@0x8005BBF4` parent call matching, with no `DIVERGES` line. The game body required no change.
