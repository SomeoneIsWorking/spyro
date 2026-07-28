---
id: C047
kind: claim
status: holds
created: 2026-07-28
tags: cd,overlay
---

## Claim

The level overlay loads once the STREAMING read primitive 0x80016698 is served with data — the port was acking those reads dataless

## Evidence

0x80016698 has 19 static call sites vs 0x80016500's 11 (verified independently with tools/callsite_args.py). Its argument contract is identical, confirmed from the body rather than assumed: the prologue moves a0->s2, a1->s4, a2->s3, a3->s1 and reads a 5th argument from sp+0x40, and at 0x800166F0 it computes a0 = s2 + (s1 >> 11), i.e. sector = baseLBA + byteOffset/2048 — the same formula cd_loader uses. The port already delivered a CD completion for every issued read, so reads through this path were told 'finished' over untouched buffers; the streaming machine sailed through all its phases with zero bytes landed and then called a level handler into never-written memory. Adding a data-serving override (game/core/cd_queue.cpp cd_stream_read) that moves the bytes then super-calls: bytes moved per run go 628736 -> 3686400, and a run logs 'stream: a0=37 dest=0x8007AA38 len=65536 a3=0x00B83800 -> moved 65536 bytes' — WAD index entry 11, one of the 36 code entries. The 0x8008772C fail-fast is GONE. Serving at issue time is not a new mechanism: our disc is synchronous, so the data IS available when the read is issued, which makes the pre-existing completion delivery truthful rather than a lie.

## What would falsify it

The 0x8008772C miss returning, or a stream: line reporting 0 bytes moved for a dest inside the arena.
