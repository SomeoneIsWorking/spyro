#!/usr/bin/env python3
"""title_prompts.py — WHICH pad button a given title screen is asking for, as a pure function.

Two drivers now play this game: `drive.py`, which blocks the product between commands over the
framework REPL, and `live_play.py`, which drives it over the live debug server while it keeps running.
The menu sequence is title knowledge, not transport knowledge, so it lives here once and both drivers
ask it. A second copy in the live driver would be free to answer a different prompt than the REPL one,
and the difference would show up as "the live run could not reach gameplay" with nothing to compare.

Each function takes a `Screen` — the guest state it is allowed to look at — and returns a `Prompt`:
the buttons to tap now, and/or the refusal that says this screen is not one this route can answer.

WHAT IS DELIBERATELY NOT ANSWERED, in both drivers, because answering it would destroy the operator's
data to reach a screenshot: the memory-card FORMAT prompts (TSM_Menu sub-states 5/6/7) and the save
OVERWRITE prompt (TSM_Loading state 2). Both only appear over existing card content.
"""

from __future__ import annotations

from dataclasses import dataclass

from title_states import (
    GS_CUTSCENE,
    GS_ENTRANCE_ANIMATION,
    GS_LEVEL_TRANSITION,
    GS_PLAYING,
    GS_TITLE_SCREEN,
    TSS_ACTIVE,
    TSM_DEMO,
    TSM_INIT,
    TSM_LOADING,
    TSM_MENU,
    TitleState,
)

# TSM_Menu sub-states that are safe to answer: 15 picks the card, 4 accepts playing without a save, 10
# confirms creating this game's save. The FORMAT prompts (5/6/7) are absent on purpose.
ANSWERABLE_MENU_PROMPTS = frozenset({4, 10, 15})
PRESS_START_PLATFORM = 3  # TSM_Init sub-state: the interactive "PRESS START" platform
NEW_OR_LOAD_PROMPT = 4     # TSM_Loading state: NEW GAME (option 0) beside LOAD GAME
SLOT_PROMPT = 1            # TSM_Loading state: which slot the new game is written into

# The gamestates a route from the save picker into gameplay is allowed to pass through on its way.
LOAD_ROUTE_STATES = frozenset(
    {GS_TITLE_SCREEN, GS_LEVEL_TRANSITION, GS_ENTRANCE_ANIMATION, GS_CUTSCENE})


@dataclass(frozen=True)
class Screen:
    """The guest state a prompt decision may read. `level_trans_hud` is the level-transition tally's
    own HUD flag, which is both what the port checks and what says the screen is still up."""

    gamestate: int
    title: TitleState
    level_trans_hud: int = 0

    @property
    def at_save_picker(self) -> bool:
        return self.gamestate == GS_TITLE_SCREEN and self.title.mode == TSM_LOADING

    @property
    def playing(self) -> bool:
        return self.gamestate == GS_PLAYING


@dataclass(frozen=True)
class Prompt:
    buttons: tuple[str, ...] = ()
    reached: bool = False   # this leg of the route is finished
    refuse: str | None = None

    def __bool__(self) -> bool:
        return bool(self.buttons)


def title_menu_prompt(screen: Screen) -> Prompt:
    """Boot -> the memory-card front end. Start is only meaningful at TSM_Init sub-state 3; every
    other TSM_Init sub-state is the fly-in, a fade, or the demo hand-off, where a press would either do
    nothing or cancel the attract sequence."""
    if screen.at_save_picker:
        return Prompt(reached=True)
    if screen.gamestate == GS_TITLE_SCREEN and screen.title.mode == TSM_INIT \
            and screen.title.sub_state == PRESS_START_PLATFORM:
        return Prompt(("start",))
    if screen.gamestate == GS_TITLE_SCREEN and screen.title.mode == TSM_MENU \
            and screen.title.sub_state in ANSWERABLE_MENU_PROMPTS:
        return Prompt(("cross",))
    return Prompt()


def new_game_prompt(screen: Screen) -> Prompt:
    """The save picker -> out of the title screen. NEW GAME must be selected explicitly: confirming
    the picker on its default LOAD GAME over empty slots has nothing to load and bounces back to the
    title, which is what silently ended earlier runs."""
    if screen.gamestate != GS_TITLE_SCREEN:
        return Prompt(reached=True)
    if screen.title.mode == TSM_LOADING and screen.title.state == NEW_OR_LOAD_PROMPT:
        # The option is already NEW GAME: confirm. Otherwise move to it first.
        return Prompt(("cross",) if screen.title.option == 0 else ("left",))
    if screen.title.mode == TSM_LOADING and screen.title.state == SLOT_PROMPT:
        return Prompt(("cross",))
    return Prompt()


def load_route_prompt(screen: Screen, skip_transitions: bool = False) -> Prompt:
    """Out of the title screen -> GS_Playing.

    `skip_transitions` presses Start on the two screens the port itself can cancel: the level-transition
    tally, but only while its HUD flag is still set, and the "THE ADVENTURE BEGINS..." flyby on sight
    — including while the level is still streaming, because the port holds an early press until its
    load gate opens. The presses are the point of the flag: a run with it off and one with it on differ
    by nothing but the press."""
    if screen.playing:
        return Prompt(reached=True)
    if skip_transitions and screen.gamestate == GS_LEVEL_TRANSITION and screen.level_trans_hud != 0:
        return Prompt(("start",))
    if skip_transitions and screen.gamestate == GS_TITLE_SCREEN \
            and screen.title.mode == TSM_DEMO and screen.title.state == TSS_ACTIVE:
        return Prompt(("start",))
    if screen.gamestate not in LOAD_ROUTE_STATES:
        return Prompt(refuse=f"left the load route into unexpected gamestate {screen.gamestate}")
    return Prompt()
