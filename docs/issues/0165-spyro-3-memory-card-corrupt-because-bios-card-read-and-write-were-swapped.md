---
id: 165
title: Spyro 3 reported a corrupt memory card because the BIOS card read and write entries were swapped in the HLE
status: resolved
symptom: after the title's New Game the game said the memory card was corrupt and the card file's header frame was zeroed
tags: spyro3,memcard,hle
created: 2026-10-01
updated: 2026-10-01
---

## Answer

`runtime/psx/memcard.cpp` bound B0:0x4E to the frame read and B0:0x4F to the frame write. The guest's card driver
(SCUS_944.67 `0x8007E954`, called from `0x8007DFC0..0x8007E00C`) uses 0x4F to read a frame and 0x4E to write one, so
its "read" overwrote the card frame with the guest's buffer. psxport `s23-play` commit `fc735f2b` swaps them; a NULL
source in the write is the guest's no-op probe and moves nothing. `tests/test_memcard_file_api.cpp` holds both entries
and the NULL negative; it failed 2 of 12 against the old file and passes 12 of 12.

## Not claimed

The scratch card used in the investigation had been damaged by the bug (frame 0 zeroed) and was restored from the
main checkout's card; saves are not otherwise exercised by the route.
