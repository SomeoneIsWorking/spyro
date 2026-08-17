---
id: 65
title: PSXPORT_INTERP_DEPTH tap cannot give the world renderer depth (widescreen is native-only; native leg aborts in field)
status: dead-end
symptom: wants per-primitive depth for 2D/3D discrimination without the 5000-instruction transcription
tags: render,depth,widescreen
created: 2026-08-17
updated: 2026-08-17
---

The framework has a dynamic interpreter depth tap (PSXPORT_INTERP_DEPTH=1, interp.cpp: gte_hold_pz/gte_record_pz) that attaches per-vertex view-space Z to packet addresses, resolving 99.8% of vertex lookups — but it is OFF by default because it attaches spurious depths (not all real vertices) and the frame collapses. It could only help the WIDESCREEN path, which is NATIVE-ONLY (render-path-tristate, USER-confirmed: enhancements run in native and nowhere else): gpu_vk_wide_engine() requires enhancementsAllowed() (gpu_vk.cpp:261), false on the reference leg (measured wide_engine=0 under PSXPORT_RENDER_PSX=1). And the NATIVE leg aborts in the field (only ONE producer, stage-13 title sprites C167), so it can never reach the world renderer's field scene (measured rc=139). So the interp depth tap is NOT a viable cheaper path to world-renderer depth: it would need BOTH a precision fix AND fixing the native leg's field abort (adding producers), and it still arguably violates the settled 'own the producer for depth' rule. Verdict: the byte-exact transcription of 0x800258F0 remains the correct path.
