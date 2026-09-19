---
id: 129
title: Start should cancel the "THE ADVENTURE BEGINS..." flyby card
status: open
symptom: the flyby card (Spyro arcing across black while the level loads) runs to its full 384-tick animation with no way to cancel it. TransitionSkip already cancels the level-transition tally and the return-home glide; this screen is simply absent from it
state_items: S011
tags: transition,skip,input,frontend
created: 2026-09-19
---

## Requested

Operator, 2026-09-19: "I want to be able to skip these screens by pressing start".

## The screen

`GamestateCutsceneTransition` (`external/spyro-1/src/gamestates/update.c`). Reached at
`g_Gamestate == 13` (GS_TitleScreen) with `m_Mode == TSM_Demo` and `m_State == TSS_Active`; drawn by
`func_8001E6B8`. `g_TitlescreenState` is at 0x80078D78, four-byte fields:

| field | address |
|---|---|
| `m_Mode` | 0x80078D78 |
| `m_State` | 0x80078D7C |
| `m_Tick` | 0x80078D80 |
| `m_DemoType` | 0x80078D94 |

## Its terminal transition, recovered

The level path (`m_DemoType == TSD_Level`, which is the "THE ADVENTURE BEGINS..." case):

```c
if (g_LoadStage < 13) { LoadLevel(1); }
if (g_TitlescreenState.m_Tick >= 384 && g_LoadStage == 13) {
    if (m_DemoType == TSD_DemoLevel) { g_DemoMode = DEMO_MODE_PLAY; g_DemoFadeTimer = 0; }
    func_8004AC24(1);   // reset Spyro for actual gameplay
    LoadLevel(1);
    return;
}
```

**Two gates, and only one of them is presentation.** `m_Tick >= 384` is the flyby animation.
`g_LoadStage == 13` is the level actually being in memory. A skip may cancel the first and must
honour the second — dropping the load gate would be fast-forwarding past required I/O, which is the
thing the no-bandaid rule names outright.

## The design that follows

Extend `spyro1::TransitionSkip` (`titles/spyro1/core/spyro1_transition_skip.*`), which already owns
exactly this decision for two other screens and whose header states the contract: a cancellation
"performs exactly the terminal write the screen's own guest owner performs when that screen ends
naturally".

- New `Cancellation::CutsceneTransitionFlyby`.
- `classify` authorises it when `stage == 13`, `m_Mode == TSM_Demo`, `m_State == TSS_Active`,
  `m_DemoType` is a level type, **and `g_LoadStage == 13`** — so a press during the load latches and
  fires when the load completes, rather than being dropped or jumping it.
- `observe` dispatches the guest's own `func_8004AC24(1)` then `LoadLevel(1)`, on the
  `ReturnHomeSequence` precedent (which dispatches guest 0x8002C664 rather than transcribing its ten
  globals). **Do not write `m_Tick`.** Claim C179 — "Boot-logo Start clock advancement is not a valid
  skip route" — is already falsified in this repo, and a timer write is the same move.

## UNBLOCKED: LoadLevel is 0x80015370, and the terminal pair is at 0x80033158/0x80033160

`func_8004AC24` is 0x8004AC24 by its own name. `LoadLevel` is **0x80015370**, established three
independent ways from the shipping executable rather than by assumption:

1. **It owns g_LoadStage.** Of the three candidate `jal` targets inside func_8002DF9C, only
   0x80015370 both reads AND writes 0x80075864 — twenty accesses including three `sw`
   (`tools/re_globals.py --img scratch/assets/spyro1/SCUS_942.28 0x80015370`). 0x80037BD4 reads it
   once and writes it never, so it is a consumer; 0x8004A7EC never touches it.
2. **It matches the one call site already documented.** Scanning the whole 0x65800 text for
   `jal 0x80015370` gives 8 sites, one of which is 0x8002DFE8 — inside func_8002DF9C
   (0x8002DF9C..0x8002DFF8). That is the call `spyro1_transition_skip.cpp:71` already describes in
   prose, arrived at here from the other direction.
3. **It appears in the terminal pair itself**, below.

The flyby's terminal route is at:

```
0x80033158   jal 0x8004AC24     # func_8004AC24(1) -- reset Spyro for actual gameplay
0x80033160   jal 0x80015370     # LoadLevel(1)
```

Two consecutive calls eight bytes apart, exactly the decompiled `func_8004AC24(1); LoadLevel(1);
return;`. Four of the eight `LoadLevel` call sites (0x80032D34, 0x80033104, 0x80033160, 0x800334BC)
sit within 0x400 bytes of both a reference to `m_Tick` (0x80078D80) and an immediate 384, which is
the `GamestateCutsceneTransition` body; only 0x80033160 is preceded by the Spyro reset.

So the cancellation dispatches guest 0x8004AC24 then guest 0x80015370, each with a1/a0 = 1, on the
`ReturnHomeSequence` precedent. Nothing is transcribed and no timer is written.

## Acceptance

- Pressing Start on the card ends it no earlier than `g_LoadStage == 13`.
- The resulting gameplay state is indistinguishable from the uncancelled route: same load stage, same
  Spyro reset, and the oracle's decisive ranges agree with a run that let the flyby finish.
- A press before the load completes is honoured when it completes, never dropped silently.
- `drive.py --skip-transitions` covers this screen too, so the route is exercised by the gate.
