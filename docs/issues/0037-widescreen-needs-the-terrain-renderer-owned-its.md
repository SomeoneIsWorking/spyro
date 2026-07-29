---
id: 37
title: Widescreen needs the terrain renderer owned: its clip bounds are immediates, so OFX cannot be shifted independently
status: open
symptom: Widening the view requires shifting the projection centre (OFX) into a wider framebuffer, but the guest's own trivial-reject test compares the PROJECTED sx against hardcoded bounds 0 and 512<<16. Shift OFX alone and previously-visible geometry starts failing the right-hand test — the picture gets worse, not wider.
tags: gpu,widescreen,ownership,re
created: 2026-07-29
updated: 2026-07-29
---

MEASURED, so the opportunity is real: the GTE already projects ~25% of vertices outside the 512-wide frame (C127). The content exists; the game throws it away.

WHERE IT IS THROWN AWAY. 0x8004ED84-8C loads three immediate bounds into t5/t6/t7 (1<<16, 256<<16, 512<<16). 0x8004EE0C-EE3C turns them into a 4-bit clip code per vertex — sy<=0, sy>=256, sx<=0, sx>=512 — packed into the low bits of the cached vertex word. Stage 2 ANDs the three vertices' codes and skips the face when they share an off-screen side (Sutherland-Cohen trivial reject).

WHY THIS FORCES OWNERSHIP RATHER THAN A TWEAK. The bounds and the projection centre have to move TOGETHER. They cannot: OFX lives in a GTE control register the port can influence, but the bounds are immediates inside recompiled guest code. Patching guest code is not on the table (the substrate is sacrosanct, and a magic constant edit is exactly the bandaid class the project rules ban). So the honest route is to OWN this renderer natively — reimplement stage 1 (unpack, project, clip-code) and stage 2 (face walk, cull, packet emit) in C, where the bounds and the centre are ours.

THAT IS ALSO THE RIGHT DIRECTION INDEPENDENTLY: it is the 'own handlers top-down' step of the porting guide and the user's standing 'more PC driven' directive, and native depth (C125) already gives the per-vertex Z such a renderer needs.

PREREQUISITE ALREADY MET: the renderer is fully understood at instruction level — vertex format is 11/11/10-bit packed deltas, the scratchpad cache is indexed by pre-scaled byte offsets from the face list, packets are POLY_FT3 (stride 0x1C) and F3 (0x14). See issue 0034/0036 notes and the disassembly at 0x8004EBA8-0x8004EF64.

SCOPE WARNING, stated up front: this is a byte-exact reimplementation of a hand-written assembly renderer. It must be gated by the per-call differential (ndiff) against the recompiled body BEFORE any widening is switched on, or a rendering difference will be indistinguishable from a widescreen artefact.
