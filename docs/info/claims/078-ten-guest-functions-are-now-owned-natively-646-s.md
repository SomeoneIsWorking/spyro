---
id: C078
kind: claim
status: holds
created: 2026-07-28
tags: ownership,native
---

## Claim

Ten guest functions are now owned natively — ~646 static call sites — each byte-exact on 40 consecutive calls. The cos/sin table pair share one native body rather than being duplicated.

## Evidence

PSXPORT_NDIFF=40, all 10 report 40/40 matches and zero DIVERGES = 400 verified calls comparing RAM, scratchpad, all 31 GPRs and hi/lo: rand(41 callers), copy3(136), vadd(102), vsub(83), angtblA(69), angtblB(66), fill(59), zero3(40), angdist(26), vsra(24). 0x80016CB0 and 0x80016C58 are the same interpolation against tables at 0x8006CC78 and 0x8006CBF8 — exactly 128 bytes (64 entries) apart, a quarter turn, i.e. the cos/sin pair — so they share angtbl_body(base) and a fix cannot be applied to one and forgotten in the other. own_candidates.py now hides owned functions (derived from ndiff_run sites) and reports 270 leaf candidates remaining.

## What would falsify it

a recompiler change altering any of these bodies; the gate re-verifies all ten every run
