---
id: C030
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

The boot exercises exactly ONE code overlay; the other candidates need gameplay to reveal their bases

## Evidence

A 60s run logs six distinct loader calls. Three map exactly onto index entries: WAD+0x800/110592 -> base 0x801A4800 (entry 0, scores 64% = data), WAD+0x5B800/14336 -> base 0x8007AA38 (entry 2 = OVL0, the recompiled code overlay), WAD+0x7B8800/237568 -> base 0x8018B800 (entry 8, below the code threshold = data). The remaining code candidates (entries 9,11,13,15,17,... sizes 40-65KB) are never loaded during boot. So observing loads yields bases only for overlays the exercised path touches — which for boot is one.

## What would falsify it

if a longer or input-driven run loads one of those candidates, its base becomes observable and this understates what boot reaches
