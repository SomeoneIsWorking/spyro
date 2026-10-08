---
id: 177
title: Every second Spyro 2 record walks no table, so nothing interpolated
status: resolved
symptom: with fps60 on, a walking preseq was identical to fps60 off; no gameplay in-between was drawn
tags: spyro2,record,fps60,presentation
created: 2026-10-08
updated: 2026-10-08
---

## Answer

Fixed in psxport, no title change to timing. Spyro 2's gameplay steps alternate (`PSXPORT_DEBUG=boot`):
step A runs the update and the draw 0x800156FC to its limiter wait (2 fields) and seals a record
with no walk; step B resumes the draw, which flips and walks (DrawOTag 0x80055B70), then runs into
the update until its host turn ends (1 field). Each walk draws the buffer not displayed, so present N
shows the walk sealed at N-2 with an empty record between.

- `FramePresenter` (psxport 11376e21) holds only records that drew something, so it pairs the two
  shown walks across the empty record. `spyro2::FrameCut` treats an empty record as no scene change.
- `RecordRasterizer::drawInBetween` (psxport b74684e4) keeps the before-image of the last record
  with entries across empty records.

Measured on the Glimmer walk at 1x 4:3 with fps60 on: 730 in-betweens drawn, 10 refused, all 10
before gameplay (title and level fade-in, where a 1-3 entry fill record was applied after the shown
one); every gameplay in-between is drawn. Walking preseq (`scratch/record/v5/s2_43f`):
3.97 3.50 0.00 4.23 3.70 0.00 4.53 3.86 0.00 6.51 4.77 0.00 4.03 3.53 0.00, each in-between
between its neighbours (e.g. 3.97 and 3.50 against 5.78 between the reals).
