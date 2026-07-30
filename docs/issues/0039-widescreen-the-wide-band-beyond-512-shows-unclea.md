---
id: 39
title: Widescreen: the wide band beyond 512 shows uncleared texture-atlas VRAM
status: open
symptom: At 16:9 the 684-wide framebuffer renders correctly and centred with more world visible on the left, but the rightmost ~34 columns show raw texture-atlas VRAM rather than scene content.
tags: gpu,widescreen
created: 2026-07-30
updated: 2026-07-30
---

VISUALLY CONFIRMED that widescreen renders correctly and wider — this is the pixel verification C131 was missing, since a prim count is not a picture. At 16:9 the 684-wide framebuffer is correctly centred (DEMO MODE centred, Spyro mid-frame) and shows genuinely more world on the left than the 4:3 capture of the same frame. Side by side: scratch/screenshots/aspect_43.png (512) vs wide684.png (684).

THE DEFECT: the rightmost ~34 columns of the wide framebuffer show raw TEXTURE-ATLAS VRAM instead of scene content — uncleared memory, not geometry. psxport's own code already knows this hazard; gpu_native.cpp carries a comment about "raw VRAM texture-atlas garbage in the [320,nw) band. Off at 4:3".

WHY IT SHOWS NOW, and why it is expected rather than mysterious: only ONE of the six clip-bound renderers is owned and widened. Everything else still trivially rejects at 512, so the band from 512 to 684 receives terrain from the owned renderer and nothing else — and wherever nothing draws, the uncleared VRAM underneath shows through. The band is not being cleared to the scene's background across its full wide width.

TWO CANDIDATE FIXES, and they are not equivalent:
  (a) clear/fill the wide band each frame so undrawn columns are background rather than atlas garbage. Cheap, and correct regardless of how many renderers are owned.
  (b) own the remaining renderers so the band is actually drawn. That is the real fix for CONTENT, but it will not help the columns beyond where any renderer draws, so (a) is still needed.
Do (a) first: it is small, it is independent, and it makes every later widening visually assessable instead of masked by garbage.

A TOOL DEFECT FOUND THE SAME WAY, and it nearly cost a wrong conclusion: tools/shot.py cropped to a hardcoded 512. At 16:9 that shows the LEFT 512 of a 684-wide picture, which looks exactly like a shifted, cropped, broken render — I was one step from filing "widescreen is broken" against working code. It now takes --width and its help text says why. That is the third capture path in this project to mislead the same way (I008, I032, I033); the pattern is always "the instrument crops or samples differently than the thing it claims to show".

### Note (2026-07-30)
CAUSE NARROWED, and one proposed fix RULED OUT before implementing it.

THE OBVIOUS FIX IS FORBIDDEN, and the framework says so in its own comment. I had proposed "clear the wide band each frame". gpu_native.cpp's display-blank carries an explicit warning against exactly that: VRAM columns beyond the 4:3 width at the display Y are NOT framebuffer, they are the TEXTURE ATLAS (object textures and CLUTs), and a previous change that widened this clear "ZEROED the atlas — corrupting every object whose texture lives there", visible only when the game STARTED in widescreen. The margin is the RENDERER's job at present time, never a guest-VRAM clear. Reading before implementing is what caught this; the fix I filed last tick would have reintroduced a reverted bug.

A REAL BUG FOUND AND FIXED ON THE WAY (psxport): the GP0 E4 handler widened the draw-area right clip by (wide_width - 320). Hardcoded 320 again — for this 512-wide game it over-extends the draw area 192 columns past anything the renderer draws. Now relative to the live display width, identical at 320. It was NOT the cause of the strip, which persists.

WHERE THE STRIP ACTUALLY IS, measured rather than eyeballed: per-80-column colour variety across the 684-wide frame reads 50 / 139 / 176 / 258 / 360 / 893 / 709 / 564 / 429 — smooth, with no atlas-like spike, yet the image still shows noise in roughly the last 30 columns at TOP and BOTTOM only. Those are precisely the regions with no geometry coverage: sky above the horizon and ground below it. The 3D terrain that the owned renderer widened DOES reach into the band; what does not is the 2D BACKDROP.

SO THE CAUSE IS THE 2D BACKDROP NOT BEING STRETCHED to the wide width, leaving uncovered columns that present samples as atlas. psxport already has machinery for this — gpu_native.cpp widens a full-screen 2D backdrop when gpu_vk_wide_engine() && (s_prev_had3d || s_prev_had_bg2d) — so the next question is why that is not firing for this game. Worth checking whether s_prev_had3d is the gate that fails: it is set from s_seen3d, which requires a primitive to resolve per-vertex DEPTH, and depth coverage here is 2.5% (C128). If so, widescreen's backdrop stretch is silently coupled to native-depth coverage, which would be worth recording in its own right.

A TOOL TRAP HIT TWICE IN ONE TICK, both in tools/shot.py, both now fixed: it cropped to a hardcoded 512 (so a 684-wide frame read as shifted and broken — nearly filed against working code), and its buffer-choice heuristic scored colour variety over the FULL requested width, letting the highly-varied atlas columns decide which framebuffer "has the frame" and pick the wrong one. It now scores only the 4:3 columns, which are always framebuffer. Fourth and fifth instances of this project's recurring pattern: the instrument samples differently than the thing it claims to show.

### Note (2026-07-30)
SEQUENCING ESTABLISHED, and a dead end ruled out by measurement rather than argument.

WHY THE 2D BACKDROP WIDEN NEVER FIRES HERE: it is gated on (s_prev_had3d || s_prev_had_bg2d), which is LAST frame's state, and those latches are rolled every frame. This game's packet pool is double buffered, so it submits ~1600 prims on one frame and ZERO on the next — meaning s_prev_had3d is false on EVERY prim-bearing frame and the widen never runs once.

MAKING IT FIRE MADE THE PICTURE WORSE, which is the useful result. With the latch surviving an empty frame the widen runs, and screen-space elements get shifted by the margin to line up with a 3D world that OFX has re-centred in the wider frame. This port has NOT widened its 3D projection — the owned renderer widens its clip bounds but never touches OFX — so there is no such shift, and widening 2D alone misaligns 2D against 3D. The caption went from centred to cut off at the right edge. Tried, measured, reverted; the ordering is now a comment in psxport rather than something to rediscover.

SO THE ORDER IS: widen the 3D projection (shift OFX to nw/2 in the owned renderer) FIRST, then the 2D widen becomes correct rather than merely active, and only then does the uncovered-margin symptom become assessable.

A FOURTH HARDCODED 320 FOUND AND FIXED (psxport 94e52472): ws_2d_local_x centred HUD by (ww-320)/2 and stretched backdrops by ww/320. For a 512-wide game that is 182 columns instead of 86, and 2.14x instead of 1.34x. That is four instances of the same assumption in this path — wide_native_w, the GP0 E4 draw-area widen, the display blank, and now the 2D widen. Anyone extending widescreen to a non-320 game should grep the whole path for 320 before trusting any of it.

STILL OPEN: the garbage strip in the last ~30 columns at top and bottom, unchanged by any of the above. It is where nothing draws, and it cannot be fixed by clearing VRAM (that zeroes the texture atlas — see the note above). It needs the margin covered by the renderer, which needs the 3D projection widened first.

### Note (2026-07-30)
OFX RE-CENTRING IMPLEMENTED AND VERIFIED TO TAKE EFFECT — but the picture does not move, and that is an unresolved contradiction rather than something to explain away.

WHAT IS MEASURED:
  * The owned renderer now shifts the projection centre when a wide aspect is selected: logged ofx_was=01000000 (256 in 16.16) -> ofx_now=01560000 (342), i.e. +86 columns, exactly nw/2 for the 684-wide 16:9 frame. Set and RESTORED at exit, so no un-owned renderer inherits a shifted projection its 4:3 clip test would then mis-cull against.
  * 4:3 is untouched: ndiff still reports 200/200 exact, gate 16/16.
  * And the rendered frame does NOT shift. Column-brightness correlation against the pre-OFX capture gives a best-matching offset of 0 px, both over the whole frame and restricted to the terrain band (rows 60-150, x 20-620); +86 scores five times worse.

WHY THAT IS A CONTRADICTION, not an explanation: this renderer is a MAJOR contributor to the frame, not a minor one. Instrumented, it emits roughly 637 packets per call at about one call per frame, against a scene total of ~1166 prims/frame. Something emitting half the frame's primitives cannot have its projection moved 86 columns with no visible effect.

CANDIDATES, none tested — do NOT pick one by reasoning:
  (a) the visible ground and sky are a 2D BACKDROP (is3d=0) drawn by another path, so the 3D geometry this renderer emits is largely hidden behind it and the correlation is measuring the backdrop, which OFX does not affect.
  (b) the packets this renderer emits are not the ones being presented — e.g. it fills a pool that a later frame draws, so a single-frame capture compares mismatched frames.
  (c) the byte-count estimate of its share is wrong (it divides emitted bytes by 20 while packets are 20 OR 28 bytes, and it accumulates across calls), and its real share is small.

THE DECISIVE EXPERIMENT, for next time: make the owned renderer emit NOTHING (return before the face loop) and capture. Whatever disappears from the frame is exactly its visual contribution. That answers all three candidates at once and needs no reasoning — which is the right way round, because I have now inferred twice in a row and been wrong once.

The OFX code is KEPT: it is correct in itself, verified to take effect, inert at 4:3, and the ordering it establishes (3D projection before 2D widen) is what psxport's frame_finalize comment now records.

### Note (2026-07-30)
WIDESCREEN NOW WORKS, and by a route that removes the ~9150-instruction transcription queue from its critical path.

THE MOVE: the clip bounds are baked into C literals only on the RECOMPILED path. They are still ordinary words in guest RAM, and interpreting these renderers is BIT-IDENTICAL to running their recompiled bodies — verified per call against 2 MB of RAM, the scratchpad, every GPR and all of COP2 (C139). So each renderer now runs interpreted with its right bound patched to the wide width and OFX re-centred to nw/2 around the call, and the patch reverted afterwards. game/core/wide_clip.cpp; eleven bound sites across five renderers, every one read before it was listed.

That needed one framework fix: interp_run poisons r[31] with CORO_SENTINEL to spot the callee's return, and these bodies SPILL ra to a fixed save area, so the sentinel was what reached guest RAM. interp_flat stops on a plain PC compare, so the caller's real return address works as the sentinel — interp_call.

TWO MEASURED RESULTS WORTH KEEPING:
  * Widening the bounds ALONE moved FIVE pixels of a 684x240 frame (C140). The guest rejects a face only when all three vertices share a side, so with the projection still at 256 almost nothing is WHOLLY beyond 512. Bounds and OFX are not independent halves; neither does anything useful without the other.
  * Omitting ONE renderer from the table reproduced C135's misalignment exactly — the ground and characters moved by the margin and the sky did not, because 0x8004EBA8's native reimplementation is off by default and its recompiled body was still running at the 4:3 centre. It is now in the table, skipped only when PSXPORT_NATIVE_TERRAIN=1 owns it.

TWO INSTRUMENT FAULTS, BOTH MINE, BOTH IN THIS TICK:
  * The guard that checks a site really is 'lui rX,0x0200' masked with 0xFC1FFFFF, which PINS the destination register to zero — so it refused all eleven genuine sites. The run then looked exactly like 'widescreen changes nothing'. What caught it was adding a log line to the NEGATIVE path: 'called, but NOT widened: wide_engine=1 refused=1'. Before that line existed, a refusal and a never-called renderer were the same silence.
  * A 4:3-vs-16:9 offset search scored by summed squared error over a variable-width overlap, so it reported the SEARCH BOUND (+240) as the best match in all three bands. Fixed-window mean-abs-diff puts the expected +86 in the top cluster but does not separate it sharply from a spurious +179 — this scene is mostly flat sand and does not discriminate well. Do not quote that metric as confirmation; the before/after correlation (sky +85, ground +77 against a designed +86) is the one that measures something.

STILL OPEN: the garbage strip in the last ~20 columns; whether the frame is exactly centred (the offset search above cannot currently say); and 2D/HUD alignment against the re-centred 3D, which psxport's ws_2d path now has a coherent 3D projection to line up against for the first time.

### Note (2026-07-30)
THE 2D-WIDEN DIAGNOSIS IN THE NOTE ABOVE IS CORRECTED. That note said the ordering was the problem — widen the 3D projection first and the 2D widen 'becomes correct rather than merely active'. The projection is now re-centred across all five contributing renderers, so the precondition holds, and enabling the widen STILL makes the picture worse.

MEASURED, not reasoned: with the latch firing, sky rows 0-45, ground rows 90-150 and caption rows 180-210 each move a further +86 px. All three. So the widen is not shifting 2D relative to 3D at all — it shifts the WHOLE FRAME a second time on top of OFX.

THE REAL GATE IS 2D-vs-3D DISCRIMINATION (C143). psxport sets s_seen3d from a projected world prim, so the classification rides on per-primitive DEPTH; at this port's ~2.5% depth coverage almost nothing is classified 3D and 'widen the 2D' becomes 'widen everything'. The latch change is reverted and psxport's comment now records the measured cause instead of the ordering theory.

WHAT THIS MEANS FOR THE PLAN: the remaining widescreen work (this, and the garbage strip in the columns nothing draws) is now blocked on NATIVE DEPTH, which is what owning the geometry renderers buys. Ownership is back on the critical path — but for depth, not for clip bounds, and that is a different and much better-defined target than 'transcribe 9150 instructions so OFX may move'.

### Note (2026-07-30)
THE STRIP IS NOW LOCALISED EXACTLY, and the fill-rect backdrop is ruled out as its fix.

WHERE IT IS, measured instead of eyeballed (local horizontal roughness, high = atlas noise): in the right margin x=650..683 the artefact occupies DISPLAY ROWS 0-9 and 230-239 and nothing else — rows 10-229 read 0.4-9.1 against 69.1 and 159.3 for the two bad bands. It is two thin horizontal slivers at the top and bottom of the margin, not a vertical strip down the right edge, which is what it looks like by eye.

THE FILL-RECT BACKDROP IS NOT THE ANSWER. The game DOES issue a full-screen FillRect every frame (PSXPORT_DEBUG=fillrect: at=(0,8) 512x224 and at=(0,248) 512x224 alternating, full=1, wide=1), and psxport already widens such a fill into the 2D queue. Two changes to that path, both measured, both zero pixels different:
  * ungating it from the (s_prev_had3d || s_prev_had_bg2d) latch — a no-op HERE because the fill itself sets s_seen_bg2d, so the latch was already satisfied via the bg2d half. Kept anyway: a full-screen fill is classified GEOMETRICALLY, so it should not depend on a depth-derived latch, and on a port where bg2d is not set the fix would otherwise be unavailable.
  * converting the rect from VRAM-absolute to display-local before queueing. This IS a real bug — a FillRect ignores clip/offset by design so its rect is VRAM-absolute, while the 2D queue takes display-local and adds the origin back; the second buffer was passing y=248 as if it were a local coordinate. It changes nothing visible here, so it is a correctness fix rather than the strip's cause.

SO THE MARGIN IS NOT PAINTED BY THE 2D BACKDROP PATH AT ALL, and the next attempt should establish WHO paints the margin rows that ARE clean (10-229) before assuming anything about the two that are not. The 8-row and 10-row heights are suspicious: the display sits at VRAM y=8, so a source that covers 0..223 in display rows would leave exactly 224..239 uncovered at the bottom, and something covering 8..231 would leave 0..7 at the top.

Gate 16/16, and the 16:9 frame is byte-identical to the known-good capture, so neither change regressed anything.

### Note (2026-07-30)
MARGIN ARTEFACT: three fixes tried, all ZERO pixels, and the 'nothing draws there' model is dead too. Recording so the next attempt does not repeat any of it.

RULED OUT, each measured on the same frame:
  1. Ungating the widened FillRect backdrop from the (s_prev_had3d || s_prev_had_bg2d) latch — no-op here, because the fill sets s_seen_bg2d itself so the latch was already satisfied. (Kept upstream: a full-screen fill is classified GEOMETRICALLY and should not need a depth-derived latch.)
  2. Queueing that backdrop in display-local rather than VRAM coordinates — a REAL bug (the second buffer passed y=248 as if it were a local coordinate) and kept upstream as correctness, but zero pixels here.
  3. Extending the widened backdrop to the full display height. The geometry looked conclusive: the display is 512x240 and the guest's fill is 512x224, so 16 rows are never cleared, which matches slivers of ~8 rows at each end almost exactly. Zero pixels. REVERTED — an unproven behaviour change for other consumers.

AND THE OBVIOUS MODEL IS WRONG. PSXPORT_PRIMDUMP on frame 46501 (2048 prims) shows the margin columns x=650..684 ARE covered by primitives in every display-row band, including both artefact bands (115 prims in rows 0-9, 30 in rows 230-239). So it is not 'no geometry reaches those rows'.

INSTRUMENT TRAP, mine, worth remembering: the primdump's coordinates are VRAM-space, not display-space (this frame's display origin is y=248, prims run y=210..492). Comparing them against display rows first produced '0 prims overlap the margin' for EVERY band, which read as a clean answer and was a space mismatch. Also, bbox extents from this dump are not usable for 'how far right does geometry reach' — several bands report x_max=1023, the full VRAM width, because the dump includes prims in atlas space.

WHERE TO GO NEXT: the artefact is 34 columns wide and 10 rows tall at each end; the primdump says prims cover it. So the question is no longer WHO draws there but WHAT LANDS ON TOP — or whether those rows are outside the VK render target's cleared/loaded region (the margin columns are LOAD_OP_LOAD, persistent across frames, per gpu_native.cpp's own note). Check the render-target load/clear rect before touching the guest-side backdrop again.
