---
id: C009
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The framework's new Setloc tracking resolves to the correct disc LBA — Spyro is seeking WAD.WAD

## Evidence

With Cd::setloc_lba added (BCD MSF -> LBA in cd_command's Setloc case), a boot run logs exactly one position: '[cd] setloc 00:02:37 -> LBA 37'. Ground truth from the disc's own ISO9660 directory (discdump list): 'WAD.WAD LBA 37'. So the conversion is right AND the guest's post-splash spin is it waiting on the main asset archive.

## What would falsify it

if a later run logs a setloc LBA that does not correspond to a real file/extent on the disc, the BCD/MSF conversion or the capture point is wrong
