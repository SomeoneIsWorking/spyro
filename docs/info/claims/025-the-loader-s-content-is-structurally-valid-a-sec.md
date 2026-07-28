---
id: C025
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The loader's content is structurally valid (a sector-offset table), so 'the loader writes wrong bytes' is NOT supported

## Evidence

The four words at heapBase+0x174 (0x00256000, 0x037F2800, 0x0000D000, 0x037FF800) are ALL exactly 0x800-aligned — 1196, 28645, 26 and 28671 sectors. Four independent words landing exactly sector-aligned by chance is ~(1/2048)^4, i.e. essentially impossible. That is the shape of a WAD index: a table of sector offsets/sizes. So the bytes the loader placed are structured, plausible archive content, not garbage.

## What would falsify it

if these words are shown to sit at a different offset in WAD.WAD than the loader claims, or a later independent dump of the same region disagrees, the content mapping is still wrong
