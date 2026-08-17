---
id: C200
kind: claim
status: holds
created: 2026-08-17
tags: render,reference,fps60,framework
---

## Claim

The reference leg (PSXPORT_RENDER_PSX=1) presents through fps60.frame_commit, which EMITS the captured render queue — the Tomba2 shape ('frame_commit OWNS presentation in both configs', game_tomba2.cpp:135). This restores the byte-exact reference picture that the framework's ONE-PATH change (flush captures, never emits) had broken: with the previous band-aid (Fps60::reset_capture, which drained by discarding), the captured queue was never emitted to the geometry batch, so the reference leg presented rebuild_geom=0 forever — a 2-colour empty frame despite the guest submitting 1496 prims/frame. Measured after the fix: the field present went from 2 colours (empty) to 3067 colours (real gameplay frame, 99.6-100% non-black), and the title from 2 to 1445. reset_capture was removed as dead API (framework 151e5616); frame_commit is the single drain AND emitter.

## Evidence

scratch/logs/field_fix.log (field: present_4500.ppm, 3067 colours) and ref_fix1.log (title: present_1000.ppm, 1445 colours), vs presentskip.log before the fix (rebuild_geom=0, 2 colours). The geometry batch emptiness is the present_rebuild_decision input (gpu_vk_present_policy.h: PRESENT_REBUILD_GEOM fires on !geom_batch_empty); flush captures into fps60 (render_queue.cpp 'ONE PATH') and only frame_commit -> present_vk emits (fps60.cpp).

## What would falsify it

a reference-leg run whose present shows rebuild_geom=0 with a non-empty render queue, or a 2-colour present while the guest logs [gpu] frame N with prims>0
