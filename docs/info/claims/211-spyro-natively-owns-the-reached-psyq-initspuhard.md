---
id: C211
kind: claim
status: holds
created: 2026-08-21
tags: ownership,ndiff,reach,spu
depends: game/core/native_spu_hardware_init.cpp#initSpuHardwareNative, game/core/spu_hardware_init.h#spuHardwareNeedsFullReset, game/core/game_hooks.cpp#spyro_registerOverrides, tests/test_spu_hardware_init.cpp#main, external/open-spyro/src/c/InitSpuHardware.c#InitSpuHardware
---

## Claim

Spyro natively owns the reached PsyQ InitSpuHardware body at 0x8005BBF4 while retaining its generated parent and three already-owned child boundaries

## Evidence

A pre-override mixed 3,000-field FNTRACE run (`scratch/logs/gate-boot-20260821-035503.log`) reported 0x8005BBF4 once at frame 0 from ra=0x8005BA9C and the known-live 0x8005BE88 child once, while 0x80017FE4 and higher-ranked 0x800181AC were explicitly NEVER CALLED. The SCUS_942.28 slice 0x8005BBF4..0x8005BE88 is exactly 660 bytes / 165 instructions (SHA-256 `a21dba15beee45866129f9f3adeffa8fe95e3eac47fcc0781f1f5d0007b261f9`); its disassembly, Ghidra output, `external/open-spyro/src/c/InitSpuHardware.c`, and `external/spyro-1/asm/psyq.s` agree on the two-mode body and its spin/printf/PIO child boundaries.

The focused `spu_hardware_init` CTest exercises both policy answers: mode zero selects the 24-voice full reset, while 1 and 0xFFFFFFFF skip it; it also checks the exact voice tuple and both loop bounds. After repinning and rebuilding with Clang against exact psxport `9f1bb9279e8607de3fd4315dd52410726bd7ff7b`, the full CTest set passed 11/11 including cpp-policy over 41/41 compile-backed first-party C++ translation units. The proportional shipping run with `PSXPORT_NDIFF=1` (`scratch/logs/gate-boot-20260821-111354.log`) reported `spu-init@0x8005BBF4 call #1 matches the recompiled body exactly`, alongside exact spin, printf, and PIO child matches; the same run exited cleanly after 3,000 fields, reached 13 scenes, and passed the 14/14 gate. That differential compares RAM, scratchpad, GPRs, hi/lo, and COP2 state. It does not snapshot Beetle's host-side SPU state; fidelity of the MMIO effects is additionally grounded in the executable-derived store sequence using the same `Core::mem_w16` path as the retained generated body, not claimed as an independently compared state surface.

## What would falsify it

if a shipping run no longer reaches 0x8005BBF4, its executable bytes or direct-child set changes, either focused policy answer fails, PSXPORT_NDIFF reports a divergence in its compared state, or an SPU write-sequence/state comparison disagrees once that currently blind host-state surface is instrumented
