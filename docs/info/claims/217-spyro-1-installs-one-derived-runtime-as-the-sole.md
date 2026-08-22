---
id: C217
kind: claim
status: holds
created: 2026-08-22
tags: architecture,runtime,inheritance
depends: game/core/spyro_runtime.cpp#SpyroRuntime::registerOverrides, game/core/main.cpp#main, tests/test_runtime_structure.py
---

## Claim

Spyro 1 installs one derived runtime as the sole owner of context lifecycle, boot, and override registration

## Evidence

Clang build and 24/24 CTest pass; runtime_structure proves SpyroRuntime inherits LegacyGameRuntimeAdapter, main installs it before Game/Core, and the residual hook table has only four compatibility callbacks; real 3000-field boot gate exits 0 and passes 14/14.

## What would falsify it

A shipping path constructs Game before installing SpyroRuntime, a moved behavior reappears in GameHooks, or the real boot/runtime gates fail.
