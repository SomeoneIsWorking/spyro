---
id: I004
kind: instrument
status: trusted
created: 2026-09-08
---

## Instrument

tools/drive.py gameplay — reaches Artisans GS_Playing by reading g_Gamestate/g_TitlescreenState and choosing inputs, instead of replaying fixed frame counts

## Validated by

It produced the OTHER answer three times before it produced a run: it REFUSED at TSM_Menu sub_state 15 (memory-card selection unanswered), then at TSM_Loading state 4 with m_OptionSelected=1 (it was confirming LOAD GAME over three EMPTY slots, which is what had silently bounced every earlier fixed-frame script back to the title screen), then on entering GS_Cutscene. Each refusal named the observed state. After the fixes it reached GS_Playing at frame 6361 on three consecutive runs and its settled capture shows the Artisans home world with the player present. It also produced a NEW failure nobody had scripted for: the stage-8 abort now recorded as issue 0103.

## Known failure modes

(none recorded yet)
