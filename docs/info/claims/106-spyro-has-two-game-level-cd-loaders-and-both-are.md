---
id: C106
kind: claim
status: holds
created: 2026-07-29
tags: cd,loader,ownership
---

## Claim

Spyro has TWO game-level CD loaders and both are owned natively at the right layer. func_80016500 (sync, 11 static call sites) and func_80016698 (async/streaming, 19 call sites, drives level loads) share one contract: (a0 = base LBA, a1 = destination, a2 = length in bytes, a3 = BYTE OFFSET into the archive, 5th arg on the stack). Sector = a0 + a3/2048; the length is rounded UP to whole sectors (+2047 >> 11). a0 is constant 37 = WAD.WAD's LBA, so a3 is the real per-request selector — ignoring it fetches the archive's first sectors for every request, which moves the right byte COUNT to the right destination with the wrong CONTENT and therefore looks like it works. The 5th argument is constant 0x258 (600) across every observed call, so it is not a per-request parameter. Both are served natively before super-calling the recompiled body, which is the layer issue #10 argued for: the destination is an explicit ARGUMENT here rather than something to reverse-engineer out of a DMA path.

## Evidence

Disassembly of 0x80016500 shows the prologue moving a0-a3 to s4/s5/s3/s1 and reading the 5th arg at sp+72, then 'a0 = s4 + (s1 sra 11)' feeding CdIntToPos (0x80064094) and CdControl(CdlSetloc=2, 0x80063C48); length becomes (s3+2047)>>11 into 0x80076B94, dest into 0x80076B9C, busy flag 0x80076BB8=1, then CdRead (0x8006606C, mode 0x80). Live run, PSXPORT_DEBUG=cdq: both fire with varied offsets and move the full requested length — loader a3=0x00000/0x5F000/0x5F800/0x5B800/0x00800 and stream a3=0x0DF800/0x127000/0x148800/0x188800/0xB83800, moving 2048 to 292864 bytes each, arg5=0x258 every time. Gate 14/14 with arena loads UNMATCHED == 0, which depends on this path running.

## What would falsify it

A load lands the right byte count at the right address but with wrong content (would mean the a3->sector mapping or the rounding is wrong), or a call is observed with a3 not 2048-aligned, or arg5 is ever seen to differ from 0x258.
