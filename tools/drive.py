#!/usr/bin/env python3
"""drive.py — reach a named Spyro 1 game state by OBSERVING it, not by counting frames.

WHY THIS EXISTS. Every scripted route in this project so far was a fixed list of `run N` / `tap`
lines. That works exactly once: the boot/attract sequence is timing dependent, so the same script
reaches the save picker on one run, the Insomniac card on the next, and the title screen on a third
— and each of those looks like a plausible capture, so a run that never reached gameplay reads like
a run that did. Two sessions were spent re-deriving the same route from screenshots.

This drives the port's own REPL control channel as a loop: advance a few frames, READ the guest's
state words, decide the next input from them, and refuse by name when the expected state is not
reached inside the budget. Nothing is written into guest memory; every transition is taken with the
same pad edges a player would use.

The observed words come from external/spyro-1 (byte-identical target, so its symbols are our
addresses):
  g_Gamestate           0x800757D8   Gamestate enum (0 = GS_Playing, 13 = GS_TitleScreen)
  g_TitlescreenState    0x80078D78   m_Mode, m_State, m_Tick, m_SubTick, m_SubState, m_OptionSelected
  g_LoadStage           0x80075864   streaming/load phase
  g_LevelTransTicks     0x800756AC   level-transition HUD tick
  g_LevelTransHudActive 0x800756B0

Usage:
  drive.py gameplay --shot scratch/screenshots/gameplay.ppm
  drive.py gameplay --hold left --hold-frames 60 --shot scratch/screenshots/left.ppm
  drive.py gameplay --tap circle --after 120 --shot scratch/screenshots/flame.ppm
  drive.py gameplay --press-while 0:start --shot scratch/screenshots/boot-pressed.ppm
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import guest_globals
import press_conditions
import route_scenes
from repl_walk import Seeker
from spyro1_steering import Refusal as SteeringRefusal
from spyro1_steering import moby_class_targets, portal_targets

ROOT = Path(__file__).resolve().parent.parent

# The enhancement configuration every agent run of this port is gated under. It lives in the launch
# environment rather than in one tool's argparse default because oracle_compare.py builds its product
# environment from here too: left unset, the product falls back to its own discovery and picks up
# whichever psxport_settings.ini happens to sit in the working directory -- an untracked, per-machine
# file. Measured 2026-09-19: an oracle run that recorded product_env {} still had fps60 and widescreen
# on, from the operator's personal file, and a fresh clone or CI would silently have run without them.
SHIPPING_SETTINGS = ROOT / "tools" / "shipping_settings.ini"

# Guest addresses. The shared ones come from the shipping owner, game/core/guest_globals.h, through
# tools/guest_globals.py, so this driver and the product cannot read different memory. The two
# level-transition words below are read here and nowhere else, so they stay with their only reader.
G_GAMESTATE = guest_globals.kGamestate
G_TITLESCREEN = guest_globals.kTitlescreenState
G_LOAD_STAGE = guest_globals.kLoadStage
G_LEVEL_ID = guest_globals.kLevelId
G_LEVEL_TRANS_TICKS = 0x800756AC
G_LEVEL_TRANS_HUD = 0x800756B0
# The pause menu's own two words, read by the --quit-home route: D_80075720 is the selected entry
# and D_800757C8 the submenu (0 = the main list). Reached with tools/re_globals.py on the pause-menu
# update 0x8002E12C; they are read here and nowhere else, so they stay with their only reader. They
# are 0xA8 apart, so they are two reads and never one.
G_PAUSE_OPTION = 0x80075720
G_PAUSE_SUBMENU = 0x800757C8

# The gamestate / overlay vocabulary is title_states' (one home, because tools/title_prompts.py needs it
# too and a second copy is how two drivers come to disagree about which screen they are on). Re-exported
# here because this module is where every other tool has always imported them from.
from title_states import (  # noqa: F401  (re-exported on purpose — see above)
    GS_BALLOONIST,
    GS_CUTSCENE,
    GS_CREDITS,
    GS_DRAGON,
    GS_ENTRANCE_ANIMATION,
    GS_EXIT_LEVEL,
    GS_FAIRY,
    GS_FLIGHT_RESULTS,
    GS_GAME_OVER,
    GS_INVENTORY_MENU,
    GS_LEVEL_TRANSITION,
    GS_OLD_DRAGON,
    GS_PAUSE_MENU,
    GS_PLAYING,
    GS_RESPAWN,
    GS_TITLE_SCREEN,
    TSS_ACTIVE,
    TSM_DEMO,
    TSM_INIT,
    TSM_LOADING,
    TSM_MENU,
    TitleState,
)
from title_states import GAMESTATE_PRESENTATION

import title_prompts  # the menu sequence, shared with tools/live_play.py's driver

# Named so a census line reads as game states rather than as integers, and carrying EVERY state the
# enum defines so a route reports the presentations it did not reach as well as the ones it did. The
# presentation each state owns is title_states' vocabulary; this is only the name.
GAMESTATE_NAMES = {
    GS_PLAYING: "playing",
    GS_LEVEL_TRANSITION: "level_transition",
    GS_PAUSE_MENU: "pause_menu",
    GS_INVENTORY_MENU: "inventory_menu",
    GS_RESPAWN: "respawn",
    GS_GAME_OVER: "game_over",
    GS_OLD_DRAGON: "old_dragon",
    GS_FLIGHT_RESULTS: "flight_results",
    GS_DRAGON: "dragon",
    GS_ENTRANCE_ANIMATION: "entrance_animation",
    GS_EXIT_LEVEL: "exit_level",
    GS_FAIRY: "fairy",
    GS_BALLOONIST: "balloonist",
    GS_TITLE_SCREEN: "title_screen",
    GS_CUTSCENE: "cutscene",
    GS_CREDITS: "credits",
}
# One refusal type for every named refusal a driver can make, so a steering refusal is reported the
# same way as a navigation one instead of escaping as a traceback.
Refusal = SteeringRefusal


class Port:
    """One live port process driven through its REPL control channel."""

    _WORDS = re.compile(r"\[repl\] ([0-9A-F]{8}):((?: [0-9A-F]{8})+)")
    _READY = re.compile(r"\[repl\] frame=(\d+) ready")

    def __init__(self, executable: Path, binary: Path, log: Path, env: dict[str, str],
                 post_presses: "press_conditions.PostArrivalPressConditions | None" = None,
                 gamestate_address: int = G_GAMESTATE):
        # The word every sample reads. Spyro 1's by default; a title with its own game-state word
        # (tools/title_profile.py) names it here, because sampling Spyro 1's address in another
        # image reads an unrelated word and fills the census with plausible integers.
        self._gamestate_address = gamestate_address
        log.parent.mkdir(parents=True, exist_ok=True)
        self._log = log.open("w")
        self._proc = subprocess.Popen(
            [str(executable), str(binary)],
            cwd=ROOT,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self.frame = 0
        # The pad script for the screens that only exist AFTER arrival, applied from this class's own
        # sampler (see run()). It is a member rather than a Navigator's because the run spends most of
        # its time here: the level-transition tally, the entrance sweep and the return-home glide are
        # all reached by walking or by the pause menu, long after reach_gameplay() returned.
        self._post_presses = (post_presses if post_presses is not None
                              else press_conditions.PostArrivalPressConditions())
        # Every gamestate this run was ever observed in, and how many samples saw it. A driven run
        # that ends without its symptom proves nothing unless the state under test was reached, and
        # until now no driver could say: issue 0103's documented repro exits 0 either because the
        # dragon producer works or because the run never met a dragon, and the log cannot tell them
        # apart.
        self.gamestate_census: dict[int, int] = {}
        # The gamestate the most recent `run` chunk ended in, for consumers that must not re-read it.
        # -1 means "no sample yet", which is a different answer from any gamestate and is refused as
        # such by the press conditions rather than compared against 0.
        self._last_sampled_gamestate = -1
        # The census restarted at arrival. A whole-run census cannot tell "the dragon cutscene
        # rendered during gameplay" from "the attract sequence happened to pass through it on the
        # way in", and those are different answers to issue 0103.
        self._census_at_arrival: dict[int, int] = {}
        self._await_prompt()

    # -- public API ---------------------------------------------------------

    # A state the game passes through in a few frames is invisible to a sampler that only looks once
    # per call, and calls here are as long as --hold-frames. Advancing in bounded chunks costs a REPL
    # round trip each and gives every run the same resolution; the guest advances the same number of
    # frames either way, and held buttons are separate commands so chunking cannot drop an input.
    SAMPLE_FRAMES = 10

    def run(self, frames: int) -> int:
        remaining = max(1, frames)
        result = 0
        while remaining > 0:
            step = min(self.SAMPLE_FRAMES, remaining)
            self._send(f"run {step}")
            result = self._await_prompt()
            remaining -= step
            state = self.gamestate()
            self._last_sampled_gamestate = state
            self.gamestate_census[state] = self.gamestate_census.get(state, 0) + 1
            # Decided from the sample just taken, for the same reason the Navigator does it that
            # way: a second read of a state already in hand is a second REPL round trip per sample.
            for condition in self._post_presses.apply(self, state):
                print(f"press: {condition.name()} at frame {self.frame} (post-arrival)",
                      file=sys.stderr)
        return result

    def mark_arrival(self) -> None:
        self._census_at_arrival = dict(self.gamestate_census)

    @property
    def last_sampled_gamestate(self) -> int:
        """The gamestate `run` last sampled, or -1 before the first sample.

        A pre-arrival press condition is decided from this rather than from a fresh `rw`, because the
        navigator has just sampled it to choose its own next step and a second read is a second REPL
        round trip per observation for a value already in hand.
        """
        return self._last_sampled_gamestate

    def census_line(self) -> str:
        """One line naming every gamestate reached, with its denominator.

        Printed whether or not anything went wrong, and it names the states NOT seen among the ones
        a Spyro route can reach, because "the dragon cutscene never happened" and "the dragon
        cutscene rendered fine" are the two readings of a clean run that have to be told apart.
        """
        total = sum(self.gamestate_census.values())
        if not total:
            return "gamestates: nothing sampled (no frames were advanced)"
        seen = ", ".join(f"{GAMESTATE_NAMES.get(state, str(state))}={count}"
                         for state, count in sorted(self.gamestate_census.items()))
        # The states NOT seen, each with the presentation it owns: "the balloonist flight never
        # appeared" and "the balloonist flight rendered fine" are the two readings of a clean run, and
        # a bare integer is what makes them hard to tell apart in a log read days later.
        missing = [f"{name} ({GAMESTATE_PRESENTATION.get(state, '?')})"
                   for state, name in sorted(GAMESTATE_NAMES.items())
                   if state not in self.gamestate_census]
        line = (f"gamestates over {total} samples ({self.SAMPLE_FRAMES} frames apart): {seen}"
                + (f"; never reached: {', '.join(missing)}" if missing else ""))
        after = {state: count - self._census_at_arrival.get(state, 0)
                 for state, count in self.gamestate_census.items()
                 if count - self._census_at_arrival.get(state, 0) > 0}
        if not self._census_at_arrival:
            return line + "; gameplay never started, so none of this is a gameplay observation"
        since = ", ".join(f"{GAMESTATE_NAMES.get(state, str(state))}={count}"
                          for state, count in sorted(after.items()))
        return line + f" | since GS_Playing: {since or 'nothing sampled'}"

    def tap(self, button: str, frames: int = 4) -> None:
        self._send(f"tap {button} {frames}")

    def press(self, button: str) -> None:
        self._send(f"press {button}")

    def release(self, button: str) -> None:
        self._send(f"release {button}")

    def words(self, address: int, count: int = 1) -> list[int]:
        self._send(f"rw {address:X} {count}")
        return self._await_words(address, count)

    def word(self, address: int) -> int:
        return self.words(address, 1)[0]

    def gamestate(self) -> int:
        return self.word(self._gamestate_address)

    def pause_menu(self) -> tuple[int, int]:
        """The pause menu's selected entry (`D_80075720`) and its submenu flag (`D_800757C8`).

        Two reads, not one: the two words are 0xA8 apart (measured with tools/re_globals.py over the
        pause-menu update's 640 decoded instructions, where `0x80075720` has 30 accesses and
        `0x800757C8` has 5), so a single `rw` at the first would have returned the second field of
        whichever struct follows it and the route would have read a submenu that was never there.
        """
        return self.word(G_PAUSE_OPTION), self.word(G_PAUSE_SUBMENU)

    def title(self) -> TitleState:
        w = self.words(G_TITLESCREEN, 6)
        return TitleState(w[0], w[1], w[2], w[3], w[4], w[5])

    def gates(self) -> None:
        """Ask the port to list this level's gates, their target levels and path nodes."""
        self._send("gates")

    def gate_teleport(self, gate: int, node: int) -> None:
        """Place Spyro on a gate's own path node, through the port's existing gate diagnostic.

        Walking to an Artisans portal stalls about 11k view units out: the portals sit above the
        hub and the steering loop has no climb. This is the diagnostic route the port already
        provides for reaching one, and it is a driving aid, never something a product path may do.
        """
        self._send(f"gate-teleport {gate} {node}")

    def shot(self, path: str) -> None:
        Path(ROOT / path).parent.mkdir(parents=True, exist_ok=True)
        self._send(f"shot {path}")

    def dumpram(self, path: str) -> None:
        Path(ROOT / path).parent.mkdir(parents=True, exist_ok=True)
        self._send(f"dumpram {path}")

    def repl_command(self, line: str) -> None:
        """Send one line to the product's own REPL and let it answer.

        The dispatch is the product's: each owner declines the commands it does not answer, so this
        tool never has to know which owner exists or what any of them prints. That is what keeps a
        diagnostic owner the one place its output is shaped.
        """
        self._send(line)

    def preseq(self, count: int, directory: str) -> None:
        """Dump the next `count` PRESENTED frames, so a per-present question has a denominator.

        `shot` answers "what does one present look like". This is the port's own
        `preseq <N> [dir]` REPL command, which is the only thing here that writes a SEQUENCE: a
        screen-space overlay is submitted once per field into a double-buffered picture, so whether
        it is on the picture at all is a question about the sequence, not about one sample of it.
        """
        target = ROOT / directory
        target.mkdir(parents=True, exist_ok=True)
        self._send(f"preseq {count} {target}")

    def end(self) -> int:
        # A port that already exited (it stopped on its own, which is what a refused run is) has no
        # reader for `end`; reap it instead of writing to a closed pipe and burying the real refusal.
        if self._proc.poll() is None:
            self._send("end")
            assert self._proc.stdin is not None
            self._proc.stdin.close()
        self._drain()
        return self._proc.wait()

    # -- plumbing -----------------------------------------------------------

    def _send(self, line: str) -> None:
        assert self._proc.stdin is not None
        self._proc.stdin.write(line + "\n")
        self._proc.stdin.flush()

    def _lines(self):
        assert self._proc.stdout is not None
        for line in self._proc.stdout:
            self._log.write(line)
            yield line

    def _await_prompt(self) -> int:
        for line in self._lines():
            match = self._READY.search(line)
            if match:
                self.frame = int(match.group(1))
                return self.frame
        raise Refusal("the port exited before reaching its next frame boundary")

    def _await_words(self, address: int, count: int) -> list[int]:
        for line in self._lines():
            match = self._WORDS.search(line)
            if match and int(match.group(1), 16) == address:
                values = [int(v, 16) for v in match.group(2).split()]
                if len(values) == count:
                    return values
        raise Refusal(f"the port exited before answering rw 0x{address:08X}")

    def _drain(self) -> None:
        for _ in self._lines():
            pass
        self._log.close()



class Navigator:
    """Boot -> title -> save picker -> a loaded, playable level, decided from guest state."""

    STEP = 20  # frames between observations; small enough to catch a one-shot menu state

    def __init__(self, port: Port, budget: int = 12000, skip_transitions: bool = False,
                 presses: "press_conditions.PressConditions | None" = None,
                 skip_button: str = "start"):
        self._port = port
        self._budget = budget
        self._skip_transitions = skip_transitions
        self._skip_button = skip_button
        # The pre-arrival pad script lives HERE, not in main(), because the guarantee it rests on is
        # structural: only _advance() applies it, only the three pre-arrival phases call _advance(),
        # and reach_gameplay() returns before any of them can run again. So a --press-while spec
        # cannot fire during gameplay, where Start is the pause button and the next gamestate's
        # producer may not exist at all. The screens that DO come after arrival are driven by
        # Port.run()'s own sampler instead; see tools/press_conditions.py.
        self._presses = presses if presses is not None else press_conditions.PressConditions()
        # The (screen, button) presses this route has already delivered, for the whole run.
        # load_route_prompt() is consulted once per observation step and derives its answer from the
        # guest's CURRENT screen struct; after a cancellation the same struct can still read like the
        # card the guest just left, so without this the same screen is pressed again, and with Start
        # that re-fire opened GS_PauseMenu mid-route in a live run (exit 2). The key is (screen,
        # button) and not screen alone because one screen can need two DIFFERENT presses, and the
        # picker's own selection is a navigation that must repeat until the guest's option word reads
        # NEW GAME -- title_prompts gives those two actions separate targets so both survive.
        self._cancelled: set[tuple[str, str]] = set()

    def reach_gameplay(self) -> None:
        self._reach_title_menu()
        self._start_new_game()
        self._wait_for_playing()

    def _advance(self) -> None:
        """Run one observation step, then apply any pre-arrival edge its condition now authorises.

        Every pre-arrival phase advances through here, so a `--press-while` spec reaches the boot
        logos, the attract fly-in and the transition screens between them -- the states --hold, --tap
        and --after cannot, because those are applied only after arrival at GS_Playing.
        """
        self._port.run(self.STEP)
        for condition in self._presses.apply(self._port, self._port.last_sampled_gamestate):
            print(f"press: {condition.name()} at frame {self._port.frame}", file=sys.stderr)

    def _screen(self) -> "title_prompts.Screen":
        return title_prompts.Screen(gamestate=self._port.gamestate(), title=self._port.title(),
                                    level_trans_hud=self._port.word(G_LEVEL_TRANS_HUD))

    def _answer(self, prompt: "title_prompts.Prompt") -> None:
        if prompt.refuse:
            raise Refusal(prompt.refuse)
        buttons = prompt.buttons
        if buttons and prompt.target and not prompt.repeatable:
            buttons = tuple(b for b in buttons if (prompt.target, b) not in self._cancelled)
            if buttons:
                self._cancelled.update((prompt.target, b) for b in buttons)
                print(f"cancel {prompt.target}: {'+'.join(buttons)}"
                      f"{f' held {prompt.hold_frames} fields' if prompt.hold_frames else ''}"
                      f" at frame {self._port.frame}", file=sys.stderr)
        for button in buttons:
            # A screen whose own route needs a HELD button says so; a four-field edge into it would
            # be a press that changed nothing and still looked like a test.
            self._port.tap(button, prompt.hold_frames or 4)

    # WHICH button each screen wants is title_prompts' decision, not this driver's: tools/live_play.py
    # plays the same route over the live debug server, and a second copy of the menu sequence would be
    # free to answer a different prompt than this one. What stays here is the stepping and the refusal
    # wording, which is about this driver's budget.
    #
    # The prompts themselves: TSM_Init sub-state 3 is the interactive "PRESS START" platform and every
    # other TSM_Init sub-state is the fly-in, a fade, or the demo hand-off, where Start is meaningless.
    # TSM_Menu is the memory-card front end, and only its non-destructive prompts are answered (15 picks
    # the card, 4 accepts playing without a save, 10 confirms creating this game's save); the FORMAT
    # prompts 5/6/7 are deliberately absent, because a driver that answered them would erase the
    # operator's card to reach a screenshot.
    def _reach_title_menu(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.title_menu_prompt(self._screen())
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._advance()
        raise Refusal(
            f"never reached the save picker (TSM_Loading) within {self._budget} frames; "
            f"last gamestate={self._port.gamestate()} title={self._port.title()}"
        )

    # TSM_Loading state 4 offers NEW GAME (option 0) beside LOAD GAME (option 1); state 1 then picks
    # the slot the new game is written into, and state 5 is the fade towards play. Confirming state 4
    # on its default option is what silently bounced earlier runs back to the title screen: LOAD GAME
    # on three EMPTY slots has nothing to load. State 2 (confirm overwrite) is deliberately never
    # answered here — it only appears over an existing save, which is the operator's.
    def _start_new_game(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.new_game_prompt(self._screen())
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._advance()
        raise Refusal(
            f"the save picker never committed a slot within {self._budget} frames; "
            f"last title={self._port.title()}"
        )

    def _wait_for_playing(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.load_route_prompt(self._screen(), self._skip_transitions,
                                                      self._skip_button)
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._advance()
        raise Refusal(
            f"never reached GS_Playing within {self._budget} frames; "
            f"gamestate={self._port.gamestate()} load_stage={self._port.word(G_LOAD_STAGE)}"
        )


def quit_to_home(port: Port, budget: int = 4000) -> None:
    """Pause the game and take its own Quit route, which is the only way into GS_ExitLevel.

    This is the route the return-home glide needs to be observable at all: nothing else enters
    gamestate 10, so without it that arm's "recovered route" is a claim no run can check. The
    decision is made from the guest's own menu words (title_prompts.pause_quit_prompt), never from a
    fixed count of taps, and a homeworld is refused BY NAME because there Quit opens a confirm screen
    instead of leaving the level -- a route that walked into it would sit pressing Start at a prompt
    it cannot answer.
    """
    level = port.word(G_LEVEL_ID)
    if level % 10 == 0:
        raise Refusal(
            f"--quit-home needs a sub-level: level {level} is a homeworld, where the pause menu's "
            f"Quit opens a confirm screen instead of calling the exit-level entry (func_8002C618). "
            f"Cross a portal first (--seek-portal) or teleport onto a gate (--gate-teleport)"
        )
    port.tap("start")
    spent = 0
    while spent < budget:
        state = port.gamestate()
        if state == GS_EXIT_LEVEL:
            print(f"reached GS_ExitLevel at frame {port.frame}", file=sys.stderr)
            return
        if state == GS_FLIGHT_RESULTS:
            # A flight level's Quit is a different arm: `func_8002E12C` sends it to GS_FlightResults
            # and sets its own flag rather than calling the exit-level entry, so there is no glide to
            # reach from here. Said by name instead of left to spin out the budget.
            raise Refusal(
                f"--quit-home reached GS_FlightResults in level {level}: a flight level's Quit is the "
                f"results screen, not the return-home glide (func_8002E12C's g_IsFlightLevel arm)"
            )
        option, submenu = port.pause_menu()
        prompt = title_prompts.pause_quit_prompt(state, option, submenu)
        if prompt.refuse:
            raise Refusal(prompt.refuse)
        for button in prompt.buttons:
            print(f"quit-home: gamestate {state} option {option} submenu {submenu} -> {button}",
                  file=sys.stderr)
            port.tap(button)
        port.run(Navigator.STEP)
        spent += Navigator.STEP
    raise Refusal(f"the pause menu never handed off to GS_ExitLevel within {budget} fields; "
                  f"last gamestate={port.gamestate()}")


def environment(disc: str | None, settings: Path | None = None) -> dict[str, str]:
    """The headless REPL launch environment for the built port. The framework's launch policy
    (external/psxport/tools/port/launch_environment.py) owns the headless/silent/unpaced knobs so
    no agent driver can seize the desktop or drift from the others.

    `settings` names the tracked .ini the run is gated with, defaulting to the shipping one. The
    picture oracle passes the reference configuration instead: it photographs the product against a
    4:3 console, and a widescreen frame is a different SIZE, which the comparison would refuse."""
    sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))
    from port.launch_environment import agent_environment

    env = agent_environment(dict(os.environ), settings or SHIPPING_SETTINGS)
    env.update(
        PSXPORT_REPL="1",
        PSXPORT_WATCHDOG="0",
        PSXPORT_ASSET_DIR="external/psxport",
    )
    if disc:
        env["PSXPORT_SPYRO_DISC"] = disc
    return env


def disc_path(variable: str = "PSXPORT_SPYRO_DISC") -> str | None:
    """The disc named by `variable` in the environment, else in the repository's `.env`. The default is
    Spyro 1's; `boot_run.py` asks for the Spyro 2 and 3 variables through the same reader."""
    value = os.environ.get(variable)
    if value:
        return value
    env_file = ROOT / ".env"
    if env_file.exists():
        for line in env_file.read_text().splitlines():
            key, separator, rest = line.partition("=")
            if separator and key.strip() == variable:
                return rest.strip()
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("target", choices=["gameplay"], help="the state to drive to")
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/drive.log")
    parser.add_argument("--debug", default="", help="PSXPORT_DEBUG channels for this run")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="extra PSXPORT_* knob for this run, e.g. --env PSXPORT_RENDER_PATH=gte",
    )
    parser.add_argument(
        "--settings",
        default=str(SHIPPING_SETTINGS),
        help="PSXPORT_SETTINGS for this run; the default turns on the two enhancements under test "
        "(widescreen and interpolated 60fps), because the previous default named a file that did "
        "not exist and silently gated the product with both of them off",
    )
    parser.add_argument(
        "--settle",
        type=int,
        default=120,
        help="frames to run after arrival before any input; the level fades in and hands the player "
        "control some frames after GS_Playing is entered, so an input issued at frame 0 is eaten",
    )
    parser.add_argument(
        "--skip-transitions",
        action="store_true",
        help="press Start or Cross on every presentation on the boot->gameplay route that the game "
        "itself can be asked to end: the intro cutscene (a HELD press, which is the only shape its "
        "own route reads), the level-transition tally, and the level flyby",
    )
    parser.add_argument(
        "--skip-button",
        choices=["start", "cross"],
        default="start",
        help="which button --skip-transitions presses. Retail accepts either on all three screens, "
        "so the Cross half of the claim needs its own runs",
    )
    parser.add_argument(
        "--press-while",
        action="append",
        default=[],
        metavar="GAMESTATE:BUTTON[:FRAMES]",
        help="tap a button on the first occasion the guest is observed in that gamestate, BEFORE "
        "arrival. This is the only way to touch boot, the logos, the attract fly-in and the "
        "transition screens: --hold/--tap/--after are all applied after GS_Playing, so no driven run "
        "could press anything before it. Conditional rather than a frame count, because the "
        "boot/attract sequence is timing dependent and field 400 is a different screen on every run. "
        "Exactly one edge per spec, never a second: gamestate 0 is both the boot logo and the "
        "arrival state, and a live run that re-fired there pressed Start in gameplay and opened the "
        "pause menu. The states are "
        + ", ".join(f"{n}={s}" for s, n in sorted(press_conditions.GAMESTATE_NAMES.items()))
        + ".",
    )
    parser.add_argument(
        "--press-after",
        action="append",
        default=[],
        metavar="GAMESTATE:BUTTON[:FRAMES]",
        help="tap a button on the first occasion the guest is observed in that gamestate AT ANY TIME, "
        "including after GS_Playing. This is the only way to touch the presentation screens that sit "
        "between two levels -- the level-transition tally (1), the entrance sweep (9), the return-home "
        "glide (10) -- because --press-while stops applying at arrival and --hold/--tap/--after are "
        "frame counts, and the port enters a level transition on a portal the run walks into rather "
        "than at a field anyone can predict. Same one-edge rule; the states a button cannot skip "
        "(GS_Playing, the pause menu, the inventory) are refused by name. The states are "
        + ", ".join(f"{n}={s}" for s, n in sorted(press_conditions.GAMESTATE_NAMES.items()))
        + ".",
    )
    parser.add_argument("--hold", action="append", default=[], help="button held after arrival")
    parser.add_argument("--hold-frames", type=int, default=60)
    parser.add_argument("--tap", action="append", default=[], help="button tapped after arrival")
    parser.add_argument("--after", type=int, default=0, help="frames to run after the taps")
    parser.add_argument(
        "--repeat", type=int, default=1, help="how many times to repeat the tap/after cycle"
    )
    parser.add_argument(
        "--seek-class",
        type=int,
        default=-1,
        help="walk to the nearest live Moby of this class before the holds/taps, steering from the "
        "guest camera each step; Spyro 1's gems are classes 83..87",
    )
    parser.add_argument(
        "--seek-arrived",
        type=int,
        default=None,
        metavar="UNITS",
        help="stop the seek at this view-space distance instead of walking all the way in. Standing "
        "next to a Moby puts it in front of the camera with nothing between, which cannot exhibit an "
        "occlusion defect; stopping short is how a scene where terrain covers it is reached",
    )
    parser.add_argument(
        "--gate-teleport",
        default="",
        metavar="GATE:NODE",
        help="place Spyro on that gate's path node before anything else, using the port's own "
        "gate diagnostic; the walk cannot climb to a portal",
    )
    parser.add_argument(
        "--seek-portal",
        action="store_true",
        help="walk to the nearest level portal instead, which is how a homeworld route reaches a "
        "level; mutually exclusive with --seek-class",
    )
    parser.add_argument(
        "--scene",
        default="",
        choices=sorted(route_scenes.SCENES),
        help="drive one named scene (tools/route_scenes.py) instead of the holds/taps below: a "
        "flight level, a boss fight, a death and respawn, or the in-game save. A scene reads the "
        "guest's own words to steer and to decide it arrived, and refuses by name when it did not, "
        "so it is mutually exclusive with --seek-class/--seek-portal/--hold/--tap",
    )
    parser.add_argument(
        "--scene-proof",
        default="",
        metavar="PATH",
        help="write what the scene proved here as JSON: the target, the guest words it read and the "
        "fields it spent. A scene route that did not reach its target writes NO file, so the "
        "reach corpus can require one rather than parse a sentence out of a log",
    )
    parser.add_argument(
        "--quit-home",
        action="store_true",
        help="after the route, pause the game and take the guest's own Quit entry, which is the only "
        "way into GS_ExitLevel (the return-home glide). Refused in a homeworld by name, where Quit "
        "opens a confirm screen instead",
    )
    parser.add_argument(
        "--quit-budget",
        type=int,
        default=4000,
        help="fields --quit-home may spend answering the pause menu before refusing",
    )
    parser.add_argument(
        "--repl",
        action="append",
        default=[],
        metavar="COMMAND",
        help="send one raw line to the product's REPL at the END of the route, just before the run "
        "ends, and let the product answer it. The dispatch is the product's: each REPL owner answers "
        "for its own commands and declines the rest, so a diagnostic owner can be read without this "
        "tool knowing what it prints.",
    )
    parser.add_argument("--shot", default="", help="capture here once the route and inputs are done")
    parser.add_argument(
        "--dumpram",
        default="",
        help="write guest main RAM here once the route and inputs are done; two runs that differ only "
        "in one setting then show whether that setting reached guest state",
    )
    parser.add_argument(
        "--preseq",
        type=int,
        default=0,
        help="capture the next N PRESENTED frames into --preseq-dir, in addition to --shot. "
        "A single capture cannot tell 'this prim is not drawn' from 'this prim is drawn on the other "
        "half of the presents': the product is double buffered and a HUD quad is submitted once per "
        "field, so a one-shot measurement of whether a screen-space overlay is on the picture is a "
        "coin flip that reads like a result. The strip is the denominator (issue 0144).",
    )
    parser.add_argument(
        "--preseq-dir",
        default="scratch/screenshots/preseq",
        help="directory --preseq writes p%%04d.ppm into. It is NOT cleared for you: a stale file in "
        "here is read as one of the N presents, which is the C138 failure. Clear it yourself.",
    )
    args = parser.parse_args()

    env = environment(disc_path())
    if args.debug:
        env["PSXPORT_DEBUG"] = args.debug
    if args.settings:
        # A settings path the product cannot open is worse than none: the run proceeds on defaults
        # and looks exactly like a run that honoured the file, which is how six weeks of Spyro
        # evidence came to be collected with widescreen and fps60 off. Refuse by name instead.
        settings = Path(args.settings)
        if not settings.is_file():
            parser.error(
                f"--settings {args.settings} does not exist; the product would silently run on "
                "defaults, with the enhancements under test switched off"
            )
        env["PSXPORT_SETTINGS"] = str(settings.resolve())
    for entry in args.env:
        name, separator, value = entry.partition("=")
        if not separator:
            parser.error(f"--env expects NAME=VALUE, got {entry!r}")
        env[name] = value

    try:
        presses = press_conditions.parse_all(args.press_while)
    except press_conditions.Refusal as refusal:
        parser.error(str(refusal))
    try:
        post_presses = press_conditions.parse_post_all(args.press_after)
    except press_conditions.Refusal as refusal:
        parser.error(str(refusal))
    port = Port(ROOT / args.executable, ROOT / args.binary, ROOT / args.log, env, post_presses)
    try:
        Navigator(port, skip_transitions=args.skip_transitions, presses=presses,
                  skip_button=args.skip_button).reach_gameplay()
        print(f"reached GS_Playing at frame {port.frame}", file=sys.stderr)
        # What the pre-arrival presses did, reported at arrival. A condition that scanned the whole
        # approach and matched nothing is named, because "the press never happened" and "the screen
        # never appeared" are the two readings of a clean log and only one of them is a port defect.
        # The script is not cleared here -- it is simply never applied again, because _advance() is
        # unreachable once reach_gameplay() has returned.
        print(presses.report(), file=sys.stderr)
        # The post-arrival script is still live from here: the run is about to walk a level, and the
        # tally, the entrance sweep and the glide are all reached from inside gameplay.
        print(post_presses.report(), file=sys.stderr)
        port.mark_arrival()
        if args.settle:
            port.run(args.settle)
        if args.gate_teleport:
            gate, separator, node = args.gate_teleport.partition(":")
            if not separator:
                parser.error(f"--gate-teleport expects GATE:NODE, got {args.gate_teleport!r}")
            port.gates()
            port.gate_teleport(int(gate), int(node))
            port.run(args.settle or 1)
        if args.seek_class >= 0 and args.seek_portal:
            parser.error("--seek-class and --seek-portal name two different destinations")
        if args.scene and (args.seek_class >= 0 or args.seek_portal or args.hold or args.tap):
            parser.error(
                "--scene decides its own inputs from guest state, so it cannot be combined with "
                "--seek-class, --seek-portal, --hold or --tap"
            )
        if args.scene:
            proof = route_scenes.run_scene(args.scene, port)
            print(f"scene {proof.scene}: proved {proof.target} in {proof.frames} field(s)",
                  file=sys.stderr)
            if proof.detail:
                print(f"scene {proof.scene}: {proof.detail}", file=sys.stderr)
            if args.scene_proof:
                target = ROOT / args.scene_proof
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(proof.as_json(), indent=2) + "\n")
            port.run(1)
            if not args.shot and not args.dumpram and not args.preseq:
                code = port.end()
                print(port.census_line(), file=sys.stderr)
                print(f"run log: {args.log} (exit {code})", file=sys.stderr)
                return code
        if args.seek_class >= 0:
            Seeker(
                port,
                f"class {args.seek_class}",
                moby_class_targets(port.words, args.seek_class),
                arrived=args.seek_arrived,
            ).walk()
        elif args.seek_portal:
            entering = port.word(G_LEVEL_ID)
            Seeker(port, "portal", portal_targets(port.words), arrived=0,
                   stop=lambda: port.word(G_LEVEL_ID) != entering,
                   stop_is=f"left level {entering}").walk()
        if args.quit_home:
            quit_to_home(port, budget=args.quit_budget)
        for button in args.hold:
            port.press(button)
        if args.hold:
            port.run(args.hold_frames)
            for button in args.hold:
                port.release(button)
        for _ in range(max(1, args.repeat)):
            for button in args.tap:
                port.tap(button, 8)
            if args.after:
                port.run(args.after)
        if args.preseq:
            port.preseq(args.preseq, args.preseq_dir)
            port.run(args.preseq + 1)
        if args.shot:
            port.shot(args.shot)
        if args.dumpram:
            port.dumpram(args.dumpram)
        if args.shot or args.dumpram:
            port.run(1)
        for command in args.repl:
            port.repl_command(command)
            port.run(1)
    except Refusal as refusal:
        print(f"drive.py REFUSED: {refusal}", file=sys.stderr)
        print(f"  {port.census_line()}", file=sys.stderr)
        print(f"  {presses.report()}", file=sys.stderr)
        print(f"  {post_presses.report()}", file=sys.stderr)
        print(f"  run log: {args.log}", file=sys.stderr)
        port.end()
        return 2
    code = port.end()
    print(port.census_line(), file=sys.stderr)
    # Both pad scripts are reported at the END as well as at arrival, because the post-arrival one
    # has only just had its chance to fire: a run that walked into a portal after arrival is exactly
    # where the tally, the sweep and the glide appear, and a report printed before the walk would
    # always say "never matched".
    print(post_presses.report(), file=sys.stderr)
    print(f"run log: {args.log} (exit {code})", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
