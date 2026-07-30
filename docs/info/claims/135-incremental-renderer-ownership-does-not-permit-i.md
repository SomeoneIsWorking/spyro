---
id: C135
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,architecture
---

## Claim

Incremental renderer ownership does NOT permit incremental widescreen: widening clip bounds is safe per-renderer, but re-centring the projection is all-or-nothing across every renderer that draws a frame.

## Evidence

Settled by MUTING the owned renderer and looking at what disappeared, after two rounds of inference had produced one wrong answer.

WHAT THE OWNED RENDERER ACTUALLY DRAWS: with its packet emission skipped, the SKY and the DISTANT TERRAIN vanish from the frame while the ground plane, the characters, Spyro and the HUD caption all remain. So a single frame is composed by SEVERAL of this game's assembly renderers, and the one owned so far contributes the sky and far geometry — not the ground.

THAT EXPLAINS AN EARLIER NON-RESULT, and the non-result was my measurement's fault: shifting OFX appeared to move nothing, because I correlated rows 60-150, which is ground drawn by renderers that are NOT owned. Correlating the band the owned renderer does draw shows the shift plainly — SKY rows 0-45: best offset +79px against an expected +86 (approximate because a smooth gradient correlates loosely); GROUND rows 90-150: 0px, unchanged. OFX re-centring works exactly as designed, on exactly the content this renderer owns.

AND THAT IS WHY IT CANNOT BE USED YET. Shifting the projection in one renderer while the others keep theirs MISALIGNS the frame against itself: the plateau slides 86 columns off the ground it stands on, leaving visible seams. Verified by eye, not argued.

SO: widening the CLIP BOUNDS is incrementally safe — it never moves existing content, it only stops faces being discarded, so the frame stays self-consistent and extends to one side (asymmetric but coherent). Re-centring the PROJECTION is all-or-nothing across every renderer that contributes to a frame. Widescreen therefore cannot be finished one renderer at a time: it needs all ~5 remaining contributors owned (~8735 instructions, C133) before OFX may move at all.

The three-line OFX change is verified working and is recorded in native_terrain.cpp as commented-out intent with this reason, to go back in when the last contributing renderer is owned.

## What would falsify it

finding that one renderer draws an entire frame by itself in some scene (then OFX could move for that scene alone), or a way to re-project un-owned renderers' output at present time from recovered depth — which would decouple the two and make widescreen incremental again
