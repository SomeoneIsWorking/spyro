---
id: C187
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor,transform
depends: game/render/fx_paired_actor.cpp#build_transform, game/render/fx_paired_actor.cpp#packed_root_input, game/render/fx_paired_actor.cpp#project_rtps
---

## Claim

On the reached front-end path, Spyro's 0x80023AC4 production transform builder reconstructs all three per-layer CR0..7 tuples and packed root RTPS inputs without ambient GTE state, and its owned boot projection state reproduces CR24..26

## Evidence

scratch/logs/paired_transform_oracle_join2.log: 384/384 transform-only gates PASS over 4100 presented frames; each reports layers=3/3 regs=36/36 (including owned CR13..15) roots=6/6 vertices=238/238 mismatches=0, with nonzero CR0 and H perturbation discriminators; PSXPORT_SELFTEST=pairedpose passes 15/15 including packed negative-low borrow

## What would falsify it

any reached front-end invocation mismatches a guest CR tuple, packed root input, or projected SXY/SZ; a paired-actor call uses inline per-view OFX/OFY not represented by ProjParams; or build_transform/packed_root_input/project_rtps changes without re-running the 4100-frame oracle and both perturbation controls
