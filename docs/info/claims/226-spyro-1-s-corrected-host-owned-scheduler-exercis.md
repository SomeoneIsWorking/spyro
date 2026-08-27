---
id: C226
kind: claim
status: holds
created: 2026-08-27
tags: render,paired-actor,fps60,interpolation,runtime
depends: game/render/fx_paired_actor.cpp#spyro_paired_actor_fps60_world_pass, game/render/paired_actor_temporal_evidence.cpp#spyro_paired_actor_temporal_finish, titles/spyro1/core/spyro1_field_scheduler.cpp#FieldScheduler
reconfirmed: 2026-08-27 03:21:30
verified_at: 2026-08-27 03:21:30
---

## Claim

Spyro 1's corrected host-owned scheduler exercises paired-actor interpolation through the shipping temporal presenter

## Evidence

scratch/logs/agent-spyro-temporal-4000.stdout.log: bounded no-input native/FPS60 run, PID 2599689, PSXPORT_NATIVE_FRAMES=4000, PSXPORT_SPYRO_TEMPORAL_VERIFY=1, rc=0. Run-end proof reports eligibility=142/142, midpoint=141, endpoint=141, emitted=282/282, no_output=0, required=true => PASS; the frame-loop contract reached logic frame 1781. First eligible interval is presenter fence f2075: midpoint t=0.500 emitted 197 faces, endpoint t=1.000 emitted 196.

## What would falsify it

Any bounded no-input native/FPS60 run through the corrected scheduler reports unequal midpoint/endpoint counts, a refused or empty callback, a presentation-fence violation, guest VSync success, or the named eligibility/presenter/scheduler owners change without equivalent live re-verification

## Re-confirmed 2026-08-27 03:21:30

scratch/logs/agent-spyro-temporal-visual-3900.stdout.log: second bounded run PID 2641343 exited rc=0 with eligibility=93/93, midpoint=92, endpoint=92, emitted=184/184, no_output=0 and frame-loop contract SATISFIED. Late-start shipping presenter dumps captured 186 interp/real PNGs from f2074; visual inspection of f2164 real -> f2165 interp -> f2165 real shows coherent Spyro geometry with actor-region right edge 491 -> 495 -> 499 and centroid X 440.94 -> 443.94 -> 446.60, placing the strict midpoint between adjacent endpoint pictures. The 2D ENTERING DEMO MODE text remains verbatim/logic-rate as the documented non-tier1 residual.
