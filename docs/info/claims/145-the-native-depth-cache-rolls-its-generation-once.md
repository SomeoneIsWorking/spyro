---
id: C145
kind: claim
status: holds
created: 2026-07-30
tags: render,depth,cache
---

## Claim

The native-depth cache rolls its generation once per FRAME, but a generation is meant to be one POOL BUFFER. This game fills a buffer every OTHER frame, so an empty frame burns a generation without a flip and the buffer filled two frames ago has its depths retired before the DMA draws it — which is why exactly half the frames resolve no vertices at all. The naive fix (roll only on frames that recorded) is NOT safe: entries then outlive the address they describe, the pool reuses addresses, and stale depths are served as real ones. Half the vertices resolving correctly beats all of them resolving wrongly. The sound fix must key entries by pool-buffer identity so a reused address cannot alias.

## Evidence

PSXPORT_DEBUG=ndepth,pznear. Per sampled frame the port alternates hit=1547/miss=0 and hit=0/miss=1540 — half the frames resolve every vertex and half resolve none. The new near-miss probe settles that this is not a stride error: of 1529-1558 misses probed per frame, NONE had a recorded depth within +/-32 bytes. Rolling the generation only on frames that recorded something removes the alternation and takes resolved lookups 6.9% -> 23.0% (per-primitive 2.3% -> 8.0%) — and depth-culls the player character out of the frame (scratch/screenshots/wide_genonly.png, 3232 pixels changed against a known-good capture). Reverted; the picture is byte-identical to the known-good capture again (0 of 164160 pixels).

## What would falsify it

a key that carries pool-buffer identity — if the alternation persists once a reused address can no longer alias, the model is wrong rather than the key
