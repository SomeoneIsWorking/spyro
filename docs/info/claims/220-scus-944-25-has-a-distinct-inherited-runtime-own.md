---
id: C220
kind: claim
status: holds
created: 2026-08-22
tags: spyro2,runtime,crt0
depends: titles/spyro2/executable.json, titles/spyro2/core/spyro2_runtime.cpp#programImage_, tests/test_spyro2_runtime.cpp
---

## Claim

SCUS_944.25 has a distinct inherited runtime owning its measured executable image

## Evidence

The separately supplied 358400-byte SCUS_944.25 matches 11/11 manifest facts. psxport crt0_extract resolves the complete 8/8 boot group: bss 0x80066ED8..0x8006D264, stack words 0x80066D3C/0x80066D38, heap 0x8006D264, gp 0x80066D38, libcInit 0x8005ABD8, stores 0x8006509C/0x80065098, bias -8. Shipping decode reads jal 0x8005ABD8 at 0x80054814 and jal 0x80011ADC at 0x80054828. Spyro2Runtime inherits SpyroRuntime directly, owns those typed facts, binds no Spyro 1 legacy config/hooks, and refuses boot beyond this boundary. The Clang runtime test and full 27/27 CTest gate pass.

## What would falsify it

SCUS_944.25 bytes/hash differ, crt0 extraction disagrees with any typed runtime fact, or the Spyro 2 runtime acquires Spyro 1 legacy views
