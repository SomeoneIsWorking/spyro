---
id: 88
title: Stage-14 cutscene lacked a native scene owner
status: resolved
symptom: After selecting New Game and an empty save slot, the real product reached stage selector 14 and aborted because the native render seam owned only stage 13.
tags: render,native,cutscene,new-game,boot-to-play,re
created: 2026-08-27
updated: 2026-08-27
---

## Reproduction

At recorded framework pin `124b85c8`, an interactive real-disc native+widescreen+interpolated diagnostic selected New Game through the owned mode-2 picker. The state sequence was stage 13 mode 2 state 4 -> state 0 -> save-slot state 1 -> fade state 5 -> mode 3 loading. At field 2452 the stage selector changed 13 -> 14 and the native renderer aborted.

The run printed: `NATIVE RENDER NOT IMPLEMENTED — stage selector = 14`, arm `GS_Cutscene`, retained guest renderer `0x8001E9C8`, and `RenderWorldChunks 0x800258F0` as a reached layer. Live projection was OFX=342, OFY=120, H=341. Exit was rc 139 / SIGABRT. No guest-VSync trap fired first.

This run overlapped another game process and is therefore diagnostic frontier evidence only, not isolated final product evidence. It corrects the earlier assumption that stage 0/FIELD would be the first scene after the picker.

## Binary-grounded repair

Static RE of retained handler `0x8001E9C8` proves that stage 14 uses actor builder/submit `0x800521C0 -> 0x8001F158 -> 0x8001F798`, `RenderWorldChunks(-1)`, and cyclorama `0x8004EBA8(-1, camera+0x14, camera)` in the same authored painter domain already owned by stage 13. It additionally copies the cyclorama clear colour into both DRAWENVs before frame setup, writes world distance `0x14000`, and conditionally calls fade producer `0x800190D4(2, fade*16, fade*16, fade*16)`.

The native implementation owns each of those responsibilities in separate scene/fade modules. It
does not install a guest-render fallback or bypass a refusal.

### Resolution (2026-08-27)
Root cause: the native render seam owned only stage 13, so the first New Game transition truthfully refused reached selector 14 / retained handler 0x8001E9C8. The fix transcribes that handler's actor/world/cyclorama order, clear-colour copy, 0x14000 world distance, and conditional blend-mode-2 fade 0x800190D4 into separate native scene/fade owners with no guest fallback. Isolated real-disc validation on Clang build b50db90-dirty+psxport-3c342ec3 reached stage 14 at 16:9 (512 -> 684), captured and visually inspected one 684x240 frame plus six consecutive presents, exited rc 0, reconciled 1962 logic frames with zero dropped layers, satisfied the frame-loop contract, earned fade on seven frames, and logged no guest-VSync violation. C228 was later falsified as proof of transition completion because the run was manually ended; this issue's resolved scope is the missing stage-14 scene owner only. Issue 0089 owns the subsequent cutscene-to-gameplay acceptance boundary.
