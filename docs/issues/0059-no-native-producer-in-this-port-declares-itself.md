---
id: 59
title: No native producer in this port declares itself — 0 ProducerScope sites, so the DB can only ever report that it knows nothing
status: open
symptom: producer census reports NEVER FED on every run even though the native render path is drawing the whole picture
tags: producers,census,re,ownership
created: 2026-08-12
updated: 2026-08-12
---

Split out of #58 on 2026-08-12, which is now resolved: #58's two stated defects (an unreachable lifecycle, and a silent failure) are both fixed, and this is what is LEFT once the instrument became honest.

MEASURED at psxport pin 7dc380c5: 'grep -rn ProducerScope game/' returns **0** across this entire tree. Tomba2Engine has them in 13 files. A 400-frame capped headless run took the native path ('[render] render path = native — geometry from PC-NATIVE producers, rasterized by the PC rasterizer (SDL_GPU)'), so native producers drew the whole picture and contributed 0 notes to the census. Nothing is broken — the scopes were simply never written.

WHY THIS MATTERS MORE THAN IT LOOKS: the producer DB is the answer to 'which effects have a native producer and which are still substrate'. With no scopes, this port cannot answer that question at all, in either direction. And the failure mode is the dangerous one: a reader glancing at an empty DB could conclude 'no native producers exist' when in fact they draw everything. The framework now says so out loud rather than letting that inference stand, which is the only reason this was findable.

THE WORK, and the rule it must follow (producer_scope.h): key each scope on the GUEST submitter the native draw stands in for — never on a shared dispatcher (it would collapse every per-mode emitter into one meaningless row) and never on a shared submit leaf (it would name the library instead of the effect). Tomba2Engine's game/render/perobj_dispatch.cpp:257 is the worked example, including the DisplayPassGuard discipline that keeps a native draw inside a substrate body from violating the pc_render read-only-overlay invariant.

NOT #56, and they must not be merged: #56 is the GUEST leg unable to attribute because GameConfig::packetPoolBase is 0 (packet pool not RE'd). This is the NATIVE leg having nothing to attribute FROM. Fixing either one alone still leaves the DB unable to COMPARE the two legs, which is its whole purpose — so both are prerequisites for a real spyro producer DB.

DEPENDENCY WORTH KNOWING BEFORE STARTING: the cross-leg join is by nature cross-run (a guest leg never runs native producers; a native leg never GP0-executes guest packets), and it is carried by scratch/producers/claims.txt, which this port has never written. See Tomba2 kanban #91 — that claim file is append-only with no build provenance, so a claim fossilises when a producer's key moves. Do not build spyro's claim set without reading that first.
