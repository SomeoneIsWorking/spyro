---
id: C003
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Moving the recompiler seed set out of psxport into per-game seed files is behavior-preserving

## Evidence

Tomba!2 regenerated with its migrated game/recomp_seeds.json is byte-identical across all 129 files in generated/ (only .recomp.hash differs, written by ensure_recomp.py not emit.py); verified twice, incl. via a real tools/ensure_recomp.py <disc> run; tomba2_port builds+links; 22 psxport recomp tests pass.

## What would falsify it

if any future emit.py change alters Tomba's generated/ without a matching seed-file or RECOMP_VERSION change, the equivalence has drifted
