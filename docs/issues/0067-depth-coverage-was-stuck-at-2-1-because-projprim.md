---
id: 67
title: Depth coverage was stuck at 2.1% because ProjPrim entries were keyed by address alone, capping their lifetime at one buffer flip
status: resolved
symptom: Vertex depths are recorded in the millions and the renderer's lookups still miss 93.6% of the time; the near-miss probe reports the depths are not within +/-32 bytes of what is read ('wrong buffer'), and 2D-vs-3D discrimination sits at ~2% so widescreen re-centering shifts the whole frame
tags: depth,render,widescreen,gpu,framework
created: 2026-08-19
updated: 2026-08-19
---

ROOT CAUSE. ProjPrim keys a projected vertex's view-Z by the GUEST ADDRESS its packet word was written to. That key is a claim about memory which stops being true as soon as the guest reuses the memory — and a packet pool exists to be reused. To stop a 2D element in a recycled pool slot inheriting the depth of the 3D vertex that used to be there, entry lifetime was pinned at one buffer flip. But the generation rolls per FRAME while this game submits ~1600 prims on one frame and zero on the next, so a generation burns without a flip and the buffer filled two frames ago has its depths compacted away before the DMA draws it.

WHAT WAS RULED OUT ALONG THE WAY, because each looked like the answer:
  * 'The world renderer needs owning to get depth.' It does not. Owning 0x800258F0 (C203) changed the coverage number by nothing at all — the same 2.10% with the native body installed and with the guest body drawing.
  * 'The recompiler's pz tap is not firing.' It fires 16.7M times a run. A genuine tap gap DOES exist on the clip arm of the world renderer's projection loop (_track_value walks the fall-through path, and at 0x800262F8 the not-taken arm ends in , hopping over the arm at 0x8002631C that also stores the vertex) and a hook now supplies it — worth +1.3M records and exactly +43 3D prims out of 3.48M. Not the bottleneck.
  * 'The buffer-to-buffer carry is not running.' It runs 20.5M times and carries 10M depths to packet addresses. This was only knowable after adding a counter for it: records and lookups were both counted, the copy BETWEEN them was not, and without that number 'millions of records, 6% of lookups hit' cannot distinguish 'copies not running' from 'copies landing where nobody reads'.

FIX (framework 2de90164). An entry stores the word that was at its address when recorded and is served only while that word is unchanged, so a recycled slot cannot answer for the vertex that used to be there — it is refused and counted as STALE, separately from ABSENT. peekPz obeys the same rule, or a staged vertex whose scratchpad slot was reused donates its old depth one step earlier, invisibly. Lifetime then stops being a correctness question and retention goes to 8 generations.

RESULT: 2.10% -> 63.60% of prims with real depth, lookups 6.41% -> 85.30% resolved, 2201 stale refusals, picture byte-identical (md5 b6223ab7…), gate 13 PASS.

THE TRAP TO REMEMBER: with the guard compiled out the same run reads 70.53% — BETTER. A coverage number that improves by serving depths for words the guest has overwritten is the invisible lie the old reset() comment warned about, and it is why the previous attempt at longer lifetimes (6.9% -> 23%) shipped a depth-culled player character. Judge this number with the stale count beside it, never alone.
