---
id: C002
kind: claim
status: holds
created: 2026-09-14
tags: launcher
depends: tools/run.py#resolved_compiler, tools/run.py#compiler_mismatch
---

## Claim

A no-change ./run.sh (tools/run.py --prepare-only) compiles zero object files on a warm tree

## Evidence

docs/findings/launcher-cmake-compiler-identity.md: two consecutive warm runs took 3s and 2s with zero 'Building (C|CXX) object' lines; before the fix the same command rebuilt 489/489 objects (scratch/repro/prepare1.log vs prepare2/prepare3.log)

## What would falsify it

a warm run prints a Building (C|CXX) object line, or the CMake cache stops recording the compiler path the launcher passes
