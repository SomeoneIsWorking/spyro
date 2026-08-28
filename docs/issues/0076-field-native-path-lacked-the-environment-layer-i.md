---
id: 76
title: FIELD native path lacked the environment-layer invocation owner
status: resolved
symptom: stage 0 cannot dispatch its semantic world producer because 0x8002B9CC's occlusion-group and culling-distance setup was not owned
tags: render,field,world,re,ownership
created: 2026-08-22
updated: 2026-08-22
---

## Cause

The direct RenderWorldChunks producer was already semantic, but FIELD does not call it with the title
recipe. Retail wrapper `0x8002B9CC` first clears a `0x1C00`-byte edge-work area, selects the camera
occlusion group only when it is below the environment group count, and otherwise selects the flat
list with a stage-dependent culling distance. No typed owner represented that caller contract, so
wiring the world producer directly would have guessed its selection and transient state.

## Resolution

Added one pure `field_environment` invocation recipe and a retained-body oracle at the real
`0x800258F0` call boundary. The 5,000-present reference run in
`scratch/logs/gate-boot-20260822-181019.log` reached stage 0 and matched 523/523 wrapper calls for
selection, culling distance, all 7,168 zeroed work bytes, and one world call each; 1,555 other world
calls remained outside the joined corpus. The complete gate reported 13 PASS and zero failures against
the recorded framework pin `ad5cf802`.

This resolves the missing caller-contract owner. A later batch compiled a cohesive FIELD
environment/world owner around the same invocation and the existing semantic world recipe. On
`scratch/raw/miss_ram.bin` it prepares selection 17, distance `0x28000`, 86 sectors, 1,376 candidates,
1,039 rejected candidates, and 413 final faces without mutating the culling word or the 7,168-byte
edge-work area. Stage 0 now calls this owner; issue 0077's corrected material path and the later
serialized retained-world oracle remain required for full visual/oracle parity.
