---
id: 174
title: Spyro 2 cuts miss a camera re-placed inside one game state
status: resolved
symptom: a respawn or scripted camera jump is blended across
tags: spyro2,record,cut
created: 2026-10-08
updated: 2026-10-10
---

`spyro2::FrameCut` declared a cut when the game state (0x800681C8) or level id (0x80066F90) changed. A camera
re-placed inside one state was blended across.

## Trace (2026-10-10)

The writers of 0x80068064/0x80068070 are all inside the camera controller FUN_8001FA58 (called first by
FUN_8001D8A4): the scripted-camera keys of mode 10 are copied from the table at 0x80068028 (12-byte keys: x, y,
z, rx, ry, rz) and FUN_8001D8A4 hands them to the render camera while byte 0x80067FC9 is set. The mode
machine is `0x80067ED0` (mode, set by FUN_800216D8 `SetCameraMode`, chosen per frame by FUN_80021DC0 from the
player state table 0x80061468 and the request word 0x80067FEC) and `0x80067ED4` (sub-state). FUN_800216D8 for
modes 0, 1, 2, 6, 7, 8 and 0x12 re-derives the follow angles from the camera's own position
(`FUN_8001EE18(&0x80067F00, &0x80067EAC, &0x80069FF0)`), so those switches keep the camera where it is. The
camera is PLACED at exactly three steps, each after the guest's own fade of DAT_80066F68 to full:

| mode | step | what is placed |
|---|---|---|
| 9 | sub-state 0 -> 1 (bit 7 kept) | camera angles from 0x80067FF4 and target from 0x80068008, `0x80067FC9 = 0` |
| 0xB | sub-state 0 -> 1 | player position and facing from the respawn words 0x8006A21C/0x8006A228, camera behind it |
| 10 | sub-state 0 -> 1; 0x80 -> 0x81 | the scripted camera begins (0x80067FC9 = 1); it ends and the gameplay camera is restored |

Mode 10 entered with bit 2 of 0x80068038 goes straight to sub-state 3, which blends from the current camera
into the script (FUN_8001BCC4), so it is not a placement. A shot change inside a script has no flag: the keys
carry no cut bit, ; the placements above all sit inside the fade.

## Fix

`spyro2::FrameCut::cameraPlaced` samples (mode, sub-state) with the scene when the draw returns and declares a
cut when the machine leaves 0 (mode 9, 0xB, 10) or 0x80 (mode 10) while staying in that mode. No camera
distance is read. Test `a_cut_is_a_camera_placement_inside_one_scene` (`tests/test_spyro2_record.cpp`): 8 of 9
tests passed, 60 checks, with the rule stubbed out; 9 of 9, 76 checks, with it.

## Measured

No placement occurs on the Glimmer route by itself (modes 1, 6, 0 only; `scratch/s2-determinism/camtrace.py`
logs the machine per field). With the game's own request word set to 9 at gameplay (REPL `w 80067fec 9`, saved
camera words pointing 6000 units away; an exploration, not a product path) and fps60 on, the log shows
`cut=true ... camera=9/1` at the placement and 136 consecutive presents were captured (mean brightness of each listed, 12 spread across the
event opened): the picture darkens over the fade, is black for 9 presents, and fades in at the new place; no
present shows both places. The presents
are byte-identical to the build without the rule, because the guest's own fade is full black at the jump, so
the missing cut was invisible for these three events; the rule makes the declaration exact. Not covered: a
camera jump without a fade, if the guest has one (none was found in FUN_8001FA58 or FUN_80021DC0).
