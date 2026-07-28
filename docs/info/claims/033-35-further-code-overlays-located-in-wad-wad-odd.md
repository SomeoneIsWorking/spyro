---
id: C033
kind: claim
status: holds
created: 2026-07-28
tags: overlay
---

## Claim

35 further code overlays LOCATED in WAD.WAD (odd index entries 9..77) — but not one of their load bases is known

## Evidence

tools/wad_index.py flags 36 entries as code; a structural cross-check (function prologues 'addiu sp,sp,-N', 'jr ra' epilogues) confirms all of them and rejects the data entries, so the classification is not the scorer over-firing: entry 9 has 8 prologues / 9 jr-ra / 839 jals over 14336 words, the same shape as the VERIFIED OVL0 (entry 2: 4 / 5 / 189 over 3584), while data entry 0 has 1 prologue and zero jr-ra. The layout alternates code,data,code,data — a per-level pair. Entry 2 plus the 35 odd entries 9..77 is 36, against the 37 the public decomps describe.

## What would falsify it

A structural re-test that finds prologue/epilogue counts inconsistent with code, or an observed load showing an odd entry going somewhere that is not executed.
