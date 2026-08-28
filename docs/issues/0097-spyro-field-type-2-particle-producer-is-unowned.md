---
id: 97
title: Spyro FIELD type-2 particle producer is unowned
status: investigating
symptom: gate teleport passes cyclorama mask and aborts when the live particle list selects type 2 at 0x800573C8
tags: spyro1,particles,render,re,field,ownership
created: 2026-08-28
updated: 2026-08-28
---

## Evidence

On 2026-08-28, the native gate route reached the source-backed `gate-teleport 0 0` position, passed the visible `0x8004FEA0` mask and portal composition, then refused at `0x800573C8` because the live list selected particle type 2. The dispatcher in `external/spyro-1/asm/renderers/r_particles.s` sends type 2 to `0x80057C08`; the existing native recipe only owns type 0. The live record begins `0e020201f37b114fcd0644a27e7e7e2e` at `0x801c3bb8`.

## Proper next step

Decode `0x80057C08` packet writes, primitive type, palette/sine-table reads, ordering-table insertion, and capacity before adding a dedicated recipe/submitter. Add a positive packet-shape test and a negative malformed-record test at that boundary. Do not treat the type-0 recipe as a type-2 approximation or suppress the refusal.

## Exit condition

The type-2 producer is source-grounded and admitted by the normal native FIELD composition, with focused tests and a live gate-route run that reaches the next genuine refusal.
