# Paired actor placement in the FIELD ordering table

The native paired player renderer passed its local face bucket directly to the shared FIELD painter
key. Those buckets describe order within Spyro's model; the retail tail coalesces them into a much
smaller global depth range before appending them to the world ordering table. This explains an actor
queue containing visible geometry yet appearing behind foreground world geometry. Disabling temporal
reconstruction retained the misplaced actor, so this defect belongs to ordinary scene submission.

## Binary contract

The full renderer is `0x80023AC4..0x800258EF`, including the tail beyond the short function extent in
some symbol metadata. The retained address/raw-byte listing in
`external/spyro-1/asm/renderers/r_pete.s` was checked with the project's locked Python environment
against RAM obtained from the independent full-console oracle: **1,931 instruction words scanned,
1,931 equal, zero mismatches**. Product and oracle RAM also contained identical bytes across this
range. The listing is corroborating RE material, never a generated product input.

- `0x80023F8C..0x80023FD0` sets `depthNear = max(0, (baseMACz >> 7) - instance[39])` and
  `depthOrigin = max(0, baseMACz - (512 << control))`, with signed 32-bit wrapping before each clamp.
  `control` is the maximum active model byte 11. CR13/14/15 hold control, depthNear, and depthOrigin.
- `0x80024C38..0x80024C58` establishes local pair base `0x8006FCF4`, a 288-pair span, and local
  shift `control + 4`. Paired local buckets use this fixed base.
- `0x800257AC..0x80025800` computes the initial global byte offset from the greatest occupied pair:
  `(depthNear << 3) + (32 << control) - 8 - adjustment`, then clamps negative offsets to zero.
  With `maxPair` expressed in bytes relative to the fixed local base,
  `adjustment = ((max(0, 2048 - maxPair) << control) >> 8) << 3`, preserving unsigned shift wrapping.
- `0x80025818..0x80025890` scans descending local pairs in chunks of `256 >> control` bytes.
  Unoccupied buckets consume their place. Each chunk appends local FIFO chains to its global slot;
  the next chunk goes to the preceding global slot. The modeled normal path requires chunks of at
  least one eight-byte pair.
- `0x80025894..0x800258A0` clamps at global slot zero. The branch delay slot first subtracts eight;
  the zero case adds that eight back. The previous regular actor mapper incorrectly bounced zero
  to slot one. The shared mapper now preserves the same zero clamp for both actor families.

The renderer does not write OFX, OFY, or H anywhere in this complete range. Native rendering must use
its owned projection parameters rather than assume it publishes ambient guest GTE controls.

## Native ownership and temporal policy

`actor_ot_coalescer::map` owns the byte-chunk mapping without reading or mutating guest OT memory.
Regular actors retain their record append and local FIFO policy in `actor_global_order`; paired
endpoint and interior replay share one face emitter in `fx_paired_actor`. Isolated front-end actor
groups retain their isolated ordering contract and receive no shared FIELD key.

`paired_actor_depth` derives both exact endpoint depths and the temporal extension from captured
base-MAC Z, model control, and instance depth bias. Matching intervals retain the same control and
bias while their base Z may move. Interior origin remains continuous; only the authored signed
right-shift boundary in depthNear is quantized. Exact endpoints still use integer face resolution.

`test_actor_global_order` exercises positive mappings, empty buckets, chunk edges, shifted local
bases, invalid spans/controls, and repeated chunks clamped at zero. `test_temporal_scene` exercises
shipping endpoint emission and actual `Fps60::presentPass` merging with interleaved world bins,
distinguishable same-bucket FIFO faces, stationary unequal-depth faces, isolated groups, and moving
camera depth. Shared replay restores source FIFO within each integer local bucket even when
continuous interior depth keys differ; the isolated temporal group retains its continuous order. These synthetic
production tests cover the mapping contract; they do not establish full-console visual parity.
