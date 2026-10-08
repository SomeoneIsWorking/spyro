---
id: 179
title: Spyro 2 composed terrain differs from the device on the title screen and the intro cutscene
status: resolved
symptom: PSXPORT_DEBUG=recordcheck reports mismatched=8011..8020 on the title screen and 20k-26k in the intro cutscene for composed t=1 presents; gameplay is 0
tags: spyro2,record,fps60,terrain
created: 2026-10-08
updated: 2026-10-08
---

## Symptom

`scratch/record/v6/final/run.log` (1x 4:3, fps60 on): 375 composed lines mismatch, seq 364..1456, all
before gameplay; the 197 composed lines after seq 1456 (gameplay) and every non-composed line are 0. Title screen
(seq 364..1000): about 8,010 pixels, first pixel a one-step green difference (0x265a against
0x267a). Intro cutscene (seq ~1270..1450): 20k-26k pixels.

## Cause

Title: psxport `composeFrame` gave every rendered primitive the draw environment of the object's first
entry, so terrain linked after the title's dither-0 draw-mode packets (bins 0 and 16) drew with the
wrong dither. Fixed in psxport 0e9f0307: a primitive takes the environment in effect where it lands.

Cutscene: `guest_camera::Builder::build` multiplied `(Ry*Rz)*Rx`; 0x8001C2F8's second FUN_80059D6C
call is `Rx * (Ry*Rz)` (FUN_80059D6C loads its first argument as the GTE rotation). The two agree only
when the roll is near zero, which gameplay mostly is (one word off); the cutscene's roll put 4-5
words off. Measured with the guest's own build: angles and matrix unchanged between 0x8001C2F8 and the
draw, so the host rebuild was the only difference.

## Verified

`scratch/record/v7/orderfix/run.log` (1x 4:3, fps60 on, title, intro and walk): 2047 plain and 610
composed recordcheck lines, all mismatched=0. `test_guest_camera_builder` `rx_is_the_left_factor`.
