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
