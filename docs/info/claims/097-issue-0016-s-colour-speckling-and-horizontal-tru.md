---
id: C097
kind: claim
status: holds
created: 2026-07-29
tags: gpu,framework
---

## Claim

Issue 0016's colour speckling AND horizontal truncation are ONE bug: the game switches the display to 24bpp for its logo screens and the framework never decoded GP1(08) bit 4, so 24-bit VRAM is read as 15-bit.

## Evidence

GP1(0x08)'s handler decoded horizontal resolution, interlace and 480-line mode but not bit 4 (display-area colour depth). Adding the decode and a report shows the game doing exactly this: 'display depth -> 24-BIT (GP1(08)=08000012, 512x240)' during the logo screens, then '-> 15-bit (GP1(08)=08000002)' afterwards. Both recorded symptoms follow from that single mismatch: reading 24bpp bytes as 15bpp halfwords scrambles every pixel's colour, and since 24bpp packs 1.5 halfwords per pixel, 512 halfwords cover only ~341 of the 512 pixels — which matches the observed cut at roughly two thirds across ('UNIVERS' of 'UNIVERSAL').

## What would falsify it

if a 24bpp-aware readback still shows speckle, the depth bit is only part of it and the CLUT/upload path needs examining too — decoding the bit is not the same as honouring it
