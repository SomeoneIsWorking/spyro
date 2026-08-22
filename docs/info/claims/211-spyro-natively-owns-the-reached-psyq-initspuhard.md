---
id: C211
kind: claim
status: holds
created: 2026-08-21
tags: ownership,ndiff,reach,spu
depends: game/core/native_spu_hardware_init.cpp#initSpuHardwareNative, game/core/spu_hardware_init.h#spuHardwareNeedsFullReset, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, tests/test_spu_hardware_init.cpp#main, external/open-spyro/src/c/InitSpuHardware.c#InitSpuHardware
reconfirmed: 2026-08-22 18:45:07
verified_at: 2026-08-22 18:45:07
---

## Claim

Spyro natively owns the reached PsyQ InitSpuHardware body at 0x8005BBF4 while retaining its generated parent and three already-owned child boundaries

## Evidence

A pre-override mixed 3,000-field FNTRACE run (`scratch/logs/gate-boot-20260821-035503.log`) reported 0x8005BBF4 once at frame 0 from ra=0x8005BA9C and the known-live 0x8005BE88 child once, while 0x80017FE4 and higher-ranked 0x800181AC were explicitly NEVER CALLED. The SCUS_942.28 slice 0x8005BBF4..0x8005BE88 is exactly 660 bytes / 165 instructions (SHA-256 `a21dba15beee45866129f9f3adeffa8fe95e3eac47fcc0781f1f5d0007b261f9`); its disassembly, Ghidra output, `external/open-spyro/src/c/InitSpuHardware.c`, and `external/spyro-1/asm/psyq.s` agree on the two-mode body and its spin/printf/PIO child boundaries.

The focused `spu_hardware_init` CTest exercises both policy answers: mode zero selects the 24-voice full reset, while 1 and 0xFFFFFFFF skip it; it also checks the exact voice tuple and both loop bounds. After repinning and rebuilding with Clang against exact psxport `9f1bb9279e8607de3fd4315dd52410726bd7ff7b`, the full CTest set passed 11/11 including cpp-policy over 41/41 compile-backed first-party C++ translation units. The proportional shipping run with `PSXPORT_NDIFF=1` (`scratch/logs/gate-boot-20260821-111354.log`) reported `spu-init@0x8005BBF4 call #1 matches the recompiled body exactly`, alongside exact spin, printf, and PIO child matches; the same run exited cleanly after 3,000 fields, reached 13 scenes, and passed the 14/14 gate. That differential compares RAM, scratchpad, GPRs, hi/lo, and COP2 state. It does not snapshot Beetle's host-side SPU state; fidelity of the MMIO effects is additionally grounded in the executable-derived store sequence using the same `Core::mem_w16` path as the retained generated body, not claimed as an independently compared state surface.

## What would falsify it

if a shipping run no longer reaches 0x8005BBF4, its executable bytes or direct-child set changes, either focused policy answer fails, PSXPORT_NDIFF reports a divergence in its compared state, or an SPU write-sequence/state comparison disagrees once that currently blind host-state surface is instrumented

## Re-confirmed 2026-08-21 14:14:17

psxport issue 0010 fixed nested NDIFF's singleton snapshot corruption without changing this game body. After rebuilding with Clang against exact psxport `3418a79b624765614f3f198dc1e89632e1e650f0`, the 9,000-field reference gate (`scratch/logs/gate-boot-20260821-141206.log`) reported `spu-pio@0x8005BE88` calls #1 and #2 and parent `spu-init@0x8005BBF4` call #1 all matching in the same nested comparison, then passed 13/13 checks. The upstream shipping-API discriminator passed 2/2 tests and 8 checks: an equivalent nested pair reports zero divergences, while a mutated child reports exactly two and the equivalent parent remains matched.

## Re-confirmed 2026-08-21

Post-landing nesting-safe NDIFF reports InitSpuHardware parent call 1 and WriteSpuRamPio child calls 1-2 exact with no fabricated divergence.

## Re-confirmed 2026-08-22 18:45:07

Registration moved into Spyro1Runtime::registerOverrides. The rebuilt SCUS_942.28 shipping gate gate-boot-20260822-184226.log reported spu-init@0x8005BBF4 call #1 matching the retained body; the focused test and full 27/27 CTests pass.
