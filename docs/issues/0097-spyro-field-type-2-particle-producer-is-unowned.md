---
id: 97
title: Spyro FIELD type-2 particle producer is unowned
status: resolved
symptom: gate teleport passes cyclorama mask and aborts when the live particle list selects type 2 at 0x800573C8
tags: spyro1,particles,render,re,field,ownership
created: 2026-08-28
updated: 2026-08-29
---

## Evidence

On 2026-08-28, the native gate route reached the source-backed `gate-teleport 0 0` position, passed the visible `0x8004FEA0` mask and portal composition, then refused at `0x800573C8` because the live list selected particle type 2. The dispatcher in `external/spyro-1/asm/renderers/r_particles.s` sends type 2 to `0x80057C08`; the existing native recipe only owns type 0. The live record begins `0e020201f37b114fcd0644a27e7e7e2e` at `0x801c3bb8`.

The retained arm at `0x80057C08` writes a ten-word `POLY_FT4` packet: tag `0x09000000`, the
record's `0x2E7E7E7E` color/command word, three projected vertices, and a fourth vertex from the
second `RTPS`. Its UV words are the class-selected `ParticleTexture` entry at `D_80076278[class]`
plus `8 * (record+0x10 low byte)`: vertex 0 uses `uv0/clut`, vertex 1 uses `(u2,v0)/tpage`, vertex
2 uses `(u0,v2)/tpage`, and vertex 3 uses `uv2/tpage`. The record+0x10 high byte is the depth bias.
The source inserts the packet into `g_WorldOT[(SZ3 >> 5) - depthBias]` with the normal head-link
chain and advances the packet cursor by `0x28` bytes. The native owner preserves those material and
ordering facts and uses the source `D_8006CBF8` Q12 orientation table.

Verification: a real native REPL gate route reached `gate-teleport 0 0`, reported `particles` with
`type2=2`, and continued through the wired stage-0 sequence without a particle refusal. The route's
next coverage boundary is the FIELD actor orchestration; no type-2 refusal occurred.

## Proper next step

The type-2 recipe/submitter now owns the decoded packet family and is admitted by the live gate route.
Keep the source packet and table facts above as the boundary contract; additional particle types
remain separate work.

## Exit condition

The type-2 producer is source-grounded and admitted by the normal native FIELD composition, with
focused positive/malformed recipe tests and a live gate-route run that reaches the next FIELD actor
coverage boundary without refusal.
