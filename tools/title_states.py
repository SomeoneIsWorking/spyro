#!/usr/bin/env python3
"""title_states.py — Spyro 1's gamestates, title-overlay modes and states, and the overlay's own record.

One home for this vocabulary, because two modules now need it and neither may own it: `drive.py` (the
REPL driver) and `title_prompts.py` (which button a screen is asking for, shared by the REPL driver and
the live one). When the driver owned them, the prompt policy could not import them without a cycle,
which is pressure to copy them — and a second copy of "what gamestate 13 is" is how a driver ends up
answering a prompt the game never showed.

Values are the authenticated image's, cross-referenced against external/spyro-1:
- gamestates from `include/gamestates.h` and the dispatch table in src/gamestates/update.c;
- TSM_Init/Menu/Loading/Demo from include/titlescreen.h, TSS_Setup/Loading/Active likewise;
- TitleState's six words in declaration order (include/titlescreen.h), which is how tools/drive.py's
  REPL reads them in one `rw`.
"""

from __future__ import annotations

from dataclasses import dataclass

# g_Gamestate — the stage the main loop dispatches on (src/gamestates/update.c). Only the states a
# route can actually enter are named; the table there dispatches more, and a driver that never reaches
# one does not need a name for it.
GS_PLAYING = 0
GS_LEVEL_TRANSITION = 1
GS_ENTRANCE_ANIMATION = 9
GS_TITLE_SCREEN = 13
GS_CUTSCENE = 14
GS_CREDITS = 15

# g_TitlescreenState.m_Mode — which overlay owns the title screen.
TSM_INIT, TSM_MENU, TSM_LOADING, TSM_DEMO = 0, 1, 2, 3

# g_TitlescreenState.m_State, when the mode is TSM_Demo (the flyby card's own phases).
TSS_SETUP, TSS_LOADING, TSS_ACTIVE = 0, 1, 2


@dataclass(frozen=True)
class TitleState:
    """g_TitlescreenState's first six words: the mode, the state, two tick counters, the sub-state and
    the selected option. Read in one `rw` because they are contiguous, and interpreted nowhere here."""
    mode: int
    state: int
    tick: int
    sub_tick: int
    sub_state: int
    option: int
