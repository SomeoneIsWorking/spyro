---
id: C183
kind: claim
status: holds
created: 2026-08-14
tags: render, paired-actor, oracle
depends: game/render/paired_actor_decode.cpp#resolve_normal_faces, game/render/paired_actor_decode.cpp#compare_ordered_faces, tests/test_paired_actor_decode.cpp#main
---

## Claim

Spyro's hermetic normal 0x80023AC4 face resolver exactly transcribes generated-code acceptance and compact packet content for triangles and quads

## Evidence

generated/shard_2.c 0x80024C94..0x80025040 establishes the normal parser, NCLIP/two-sided gates, four-way quad sign table, GT3 diagonal substitutions, compact RGB/attribute/opcode writes, signed raw-depth gate and FIFO scratch-bin links. tests/test_paired_actor_decode.cpp passes discriminating negatives for the former accept-all triangle, both quad diagonals, the rejected sign pair, two-sided bypass, compact attr substitution, first-field content mismatches and an empty actual census with explicit denominators.

## What would falsify it

Any generated-code re-read that changes a normal-path inequality, vertex/material/attribute order, primitive opcode, or FIFO tie rule; any paired_actor_decode test failure; or a live packet-content oracle mismatch in a field this hermetic resolver claims to model falsifies this claim. Numeric OT-bin equality and placement relative to packets already in the global OT are explicitly outside it.
