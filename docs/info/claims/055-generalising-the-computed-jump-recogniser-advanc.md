---
id: C055
kind: claim
status: holds
created: 2026-07-28
tags: recomp,framework
---

## Claim

Generalising the computed-jump recogniser advanced the port 3931 -> 4234 frames with zero seeds

## Evidence

Four extensions to emit.py's third idiom, each forced by a real dispatcher the previous version missed, each verified by the fail-fast moving to a new address: (1) run-extent bound for DYNAMIC indices, where no constants exist; (2) UNION of constant-derived and run-derived targets, since a partly-static index yielded one target and stranded the rest of its run; (3) tolerate padding before the run (base 0x8004C620 is two nops with the first case at index 1); (4) accept srl-scaled BITFIELD indices (andi 0xFF00 then srl 5) and union a stride-8 walk, because a dispatcher's scale need not match its run's spacing (0x8004C818 scales by 16 over trampolines 8 apart). Measured after each: frames 3931 -> 3956 -> 4128 -> 4234, unmapped-RAM zero throughout, seed file holding ONE entry (the genuine fn-pointer 0x80024054). The fail-fast has left the GTE dispatch region entirely and is now at 0x80038620, a function EPILOGUE reached with a real return address — a different problem class (computed return address), not this idiom.

## What would falsify it

Unmapped-RAM reads appearing, frames dropping below 4234, or a previously-recovered table switch changing.
