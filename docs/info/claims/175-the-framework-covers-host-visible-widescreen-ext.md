---
id: C175
kind: claim
status: holds
created: 2026-08-13
tags: widescreen,renderer
depends: external/psxport/runtime/recomp/gpu_vk.cpp#GpuVkState::present
---

## Claim

The framework covers host-visible widescreen extension without exposing Spyro's texture/CLUT atlas or modifying guest VRAM.

## Evidence

A/B at present 2100: psxport bbe16a74 control rightmost 240 sink pixels had 5,543 colors, mean 0.13997; c1e10128 made the crop one black color, mean 0. Producer census retained one expected row and zero unscoped-native primitives. Framework suite 45/45.

## What would falsify it

A widescreen present shows any non-authored atlas pixel in host-only margin, native-width output changes, or the margin path writes guest VRAM.
