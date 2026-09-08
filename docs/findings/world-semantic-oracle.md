# RenderWorldChunks semantic oracle

`PSXPORT_WORLD_SCENE_ORACLE=1` arms diagnostic capture inside the retained
`0x800258F0` runtime body. The shipping semantic producer does not call that
body and does not consume its packet pool, OT, scratchpad, or GTE output.

The acceptance corpus is the final linked world stream, not the allocation
stream. RenderWorldChunks initially allocates and links oversized near faces,
then its `0x8002A0A0` adaptive stage replaces those parents with child chains.
Capturing allocations therefore includes faces the GPU never receives. The
oracle instead walks all 0x800 world-OT bins from high to low at the retail
body's epilogue, keeps only packets in this call's packet-pool span, and
uses the same `world_recipe::paintOrder` authority as the semantic recipe:
descending OT bin, descending AddPrim paint group, then ascending adaptive
child suborder. The final chain walk determines membership after replacement
and derives recursive child suborder; allocation order is not treated as final
paint order. Annotation hooks still cover every
direct, medium, and near OT insertion plus the adaptive child, deferral, and
replacement sites, so an unobserved emission family makes the capture refuse.

Each retained G3/G4/GT3/GT4 record contains only facts the final retail stream
can prove: family, final order, SXY, RGB, UV, semi-transparency, CLUT/tpage when
observable, and the actual OT bin. It deliberately does **not** claim SZ or
view-Z equality. A final GT4 packet such as the one at `0x8018E55C` carries no
vertex depth, and looking depth up by its reused packet/SXY addresses would turn
ambient `ProjPrim` history into an oracle for a value the packet never encoded.
Opaque untextured tpage is likewise not compared because no GPU primitive or
DR_MODE packet makes it observable. Builder-only sector, source-address, and
source-ordinal metadata is excluded: retail packets do not carry it, so deriving
it from the semantic builder would compare the producer to itself.

Depth has a separate falsifiable seam. The pure world projection and refinement
tests feed measured model inputs through the same shipping projection owner and
compare SXY, SZ/view-Z, FLAG, packed-YZ borrow behavior, and the medium/near
midpoint graphs. Their negative controls perturb the projection input and must
fail. The final-stream oracle and the projection seam answer different
questions; neither is presented as proof of the other's field set.

`tests/test_world_scene_oracle.cpp` proves bin-descending, group-descending,
child-suborder-ascending paint order, refuses missing paint identity, and
contains corruption negative controls that require the comparator to identify
the exact changed record and field. The moving retail corpus in
`scratch/logs/gate-boot-20260822-153121.log` ran 3,000 frames and compared 1,275
world calls without a difference, including LQ, HQ direct, medium, near,
adaptive, transition, and edge cases. This is live proof of the packet-observable
fields above, not packet proof of depth. A future executable, animated world
state, new material family, or first differing final-stream record falsifies the
admission result and must reopen the producer audit.

## Owned world endpoint source

`world_scene::capture` now owns the ordered sector-selection occurrences, including duplicates
and candidates outside the endpoint frustum, both decoded LODs, full camera coordinates and both
matrices, projection/culling policy, animation readiness, and bounded authored material/refinement
records. It retains no Core, live RAM view, projected vertices, packets, or render-queue items.
The shipping `build(Core*, ...)` delegates to the same `build(const world_source::Source&)` used
for immutable endpoint reconstruction. Camera reclassification therefore revisits source
candidates, rather than trying to recover geometry from the previous picture.

Capture preserves refusal at the consuming boundary: a malformed inactive LOD remains represented
and refuses when selected; an unresolved animation channel cannot become current merely because
its packed arrays were copied. Animation still advances once through the logic-frame owner.
`world_scene_submitter::submit` publishes the complete guest visibility table before emission,
including a valid empty recipe. Its queue-only `emit` sibling shares the draw implementation and
leaves guest RAM and scratch untouched. Presentation must use that sibling. Submission admission
owns the draw offset/area (including the widened right edge), texture window, dither and depth
projection plane. Emission consumes that small snapshot instead of revisiting live GPU state or
copying the GPU's VRAM. A regression changes all those live settings after admission and compares
the queued endpoint coordinates, depth and raster attributes; it failed on the live-read path and
passes with the owned draw state.

`Source::resourceRanges()` reports sorted, deduplicated physical half-open spans for the sector
pointer table, selected group slot and terminated bytes, selected headers, successfully decoded
LQ/HQ payloads, and retained material/refinement records. Codec layout arithmetic owns payload
extents, including the unused prefix before HQ vertices. Mutable camera/environment globals are
captured values, outside the loaded-resource spans. Invalid selection publishes no partial range
set; an invalid inactive LOD publishes no payload range. Material spans retain valid RAM-end
prefixes and signed-selector extras. These spans let temporal identity checks use psxport's
whole-range image-generation lookup; checking only a resource's starting address would miss a
newer load that replaces its interior.

The normal CTest suite includes `world_scene_prepare`, `world_hq_refinement`, and
`native_render_producers`. They exercise source destruction, previously culled candidates,
duplicate selection occurrences, LQ and textured HQ refinement, material bounds, exact endpoint
attributes, populated/empty visibility publication, refusal, and queue-only emission. These are
endpoint contracts; world/camera temporal matching and intermediate projection are not enabled
by this ownership change.

A silent widescreen product observation during this integration reached Artisans and ended at
field 4061 with 2,063 presentation fences, 22,230,736 JIT block executions and zero fallback.
The 60-field Left input retained the prior native player `(84356, 46546, 9692)` and camera
`(86564, 45526, 10301)` samples. The spawn and Left pictures differed from the preceding capture
in 5,030 and 6,457 of 492,480 RGB channel bytes respectively; the run also completed one additional
product step at that field count. This observation is not a pixel-equivalence result or a matched
oracle checkpoint. The independently sampled camera-phase limitation in issue 0102 remains open.
