---
id: 161
title: Spyro 2's retail CdControlB waited out libcd's 0x3C0-vblank deadline before every XA Setmode, freezing the title scene
status: resolved
symptom: guest stdout "CD timeout:" every ~570 steps and the title scene advancing one frame of animation per ~1000 steps
tags: spyro2,cd,cdcontrol,timeout
created: 2026-10-01
updated: 2026-10-01
---

## Answer

The game calls libcd CdControlB (`0x80058994`) at `0x800131B0` for Setmode `0xC8` (XA ADPCM). The retail body
sends the command through `0x8005CB80` and waits in the sync routine `0x8005C900` for the guest status byte
`0x800669DC` to reach Complete; that byte is raised by libcd's interrupt handler. With CdRead and CdCommand
native nothing raises it, so the wait ran out its deadline (`VSync(-1) + 0x3C0`, `0x8005C95C`) and printed
`CD timeout` (`0x8005C9CC`), 23 times in 13161 steps, each costing about 570 steps of a frozen game. Stack
walk at the stall: return addresses `0x800131B8` (game), `0x80058A74`, `0x8005CA48`.

Fix: bind `0x80058994` to the framework's `cd_control_sync` in `PlatformHlePlan::bindings`, the same owner
Spyro 1 binds for its CdControlB. Address `0x80058994` lies in the declared libcd window.

## Result

Spyro 2 reaches "Spyro Ripto's Rage" with "press start" at step 900 (before: Spyro had flown in only by step
9000). `CD timeout` count 0 in 4500 fields. Spyro 3 does not call this leaf before its title; its libcd
CdControlB equivalent was not measured.

