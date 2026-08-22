---
id: C217
kind: claim
status: holds
created: 2026-08-22
tags: architecture,runtime,inheritance
depends: game/core/spyro_runtime.cpp#SpyroRuntime::guestProgramImage, titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::registerOverrides, game/core/main.cpp#main, tests/test_runtime_structure.py, tests/test_spyro2_runtime.cpp
reconfirmed: 2026-08-22
verified_at: 2026-08-22 19:10:18
---

## Claim

Spyro 1 installs one derived runtime as the sole owner of context lifecycle, boot, and override registration

## Evidence

Clang build and 24/24 CTest pass; runtime_structure proves SpyroRuntime inherits LegacyGameRuntimeAdapter, main installs it before Game/Core, and the residual hook table has only four compatibility callbacks; real 3000-field boot gate exits 0 and passes 14/14.

## What would falsify it

A shipping path constructs Game before installing SpyroRuntime, a moved behavior reappears in GameHooks, or the real boot/runtime gates fail.

## Re-confirmed 2026-08-22 18:45:07

SpyroRuntime now directly inherits GameRuntime as the lineage root; Spyro1Runtime final owns context lifecycle, override registration, and frame-loop boot while alone binding the residual SCUS_942.28 compatibility views. Main installs it before Game/Core. Spyro2Runtime separately inherits the root and binds no Spyro 1 config/hooks. The structure/runtime gates and full 27/27 CTests pass; the real 3,000-field Spyro 1 gate exits 0 and passes 14/14.

## Re-confirmed 2026-08-22

Post-commit 987f9f8 root rebuilt the authoritative Clang tree; runtime_structure and spyro2_runtime pass within 27/27 CTests, and the Spyro 1 native gate remains 14/14.
