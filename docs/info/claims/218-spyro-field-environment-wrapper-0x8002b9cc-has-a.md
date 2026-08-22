---
id: C218
kind: claim
status: holds
created: 2026-08-22
tags: render,field,world,re
depends: game/render/field_environment_recipe.cpp#derive, game/render/field_environment_recipe.cpp#matches, game/render/field_environment_oracle.cpp#worldEntry
---

## Claim

Spyro FIELD environment wrapper 0x8002B9CC has a semantic caller recipe for the direct RenderWorldChunks producer: valid camera occlusion groups use selection=group and culling distance 0x28000; otherwise selection=-1 and stages 13/14 use 0x1C000 while every other stage uses 0x14000.

## Evidence

Ghidra decompile scratch/decomp/field_environment_8002b9cc.c from the analyzed SCUS_942.28 RAM image gives the exact branch and 0x1C00 memset. The real capped reference path scratch/logs/gate-boot-20260822-181019.log reached FIELD and the runtime comparator reported PASS 523/523 at the retained world's input boundary: exact selection/distance, zero nonzero bytes over all 7,168 work bytes, one world call per wrapper; it separately counted 1,555 foreign world calls. The complete gate reported 13 PASS and zero failures against the recorded framework pin ad5cf802. tests/test_field_environment_recipe.cpp exercises eight branch cases and the same runtime matcher with changed selection, changed distance, and uncleared-work negatives.

## What would falsify it

a real FIELD call whose observed selection, culling distance, work-area precondition, or world-call cardinality differs from the recipe; or a SCUS_942.28 byte change at 0x8002B9CC
