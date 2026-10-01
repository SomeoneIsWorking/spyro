#!/usr/bin/env python3
"""title_profile.py — the per-title facts a driver needs to play Spyro 2 or Spyro 3, in one place.

    uv run --frozen python tools/title_profile.py             # print each profile
    uv run --frozen python tools/title_profile.py --selftest

WHY THIS EXISTS. `drive.py` reads Spyro 1's game-state word, and every other title keeps its own, so
driving Spyro 2 or 3 needs the same few facts per title: which disc and image, which word holds the game
state, which values mean "title", "loading" and "playing", and where the player's position lives. They
were scattered across `boot_run.py` (identity) and nowhere (the rest). This is the one home; the route
logic (`title_route.py`) and the boot report (`boot_run.py`) both read it, and neither restates a value.

EVERY ADDRESS NAMES ITS BYTES. A game-state word is cited from the instruction pair that dispatches on
it, a position triple from the code that reads or writes it, so a reader can decode the same words with
`external/psxport/tools/disasm.py` (the PS-X EXE text loads from file offset 0x800).

  Spyro 2 (SCUS_944.25) game-state word 0x800681C8
    0x8001B160  lui $v1,0x8007 ; 0x8001B164 lw $v1,-0x7E38($v1)    the per-frame update's switch operand
                (0x8001B16C `sltiu $v0,$v1,0xD` bounds it at 13 states)
    0x80010814  the 13-entry handler table it indexes. The values the route uses were OBSERVED live, not
                read from the table: 0 = playing, 5 = loading, 6 = cutscene, 11 = title (4 is the pause menu)
    position triple 0x80067EE4 (x, y, z, one word each, z up): 0x80016AF4 `lui $s1,0x8006 ; addiu $s1,$s1,
                0x7EE4` hands its address to the routine at 0x80016AFC; the triple moved with the pad and
                z with a jump (tools/title_route.py reports both, and refuses when they do not).
    conversation state 1 was OBSERVED live (Up from the arrival point opens Pogo's talk; gamestate 1 for
                the whole conversation, then back to 0).
    level identity: 0x80014450..0x8001447C `lw 0x7118($v0)` (homeworld, word 0x80067118) `sll 5`, `+ lw
                0x6F54($v1)` (level in homeworld, word 0x80066F54) `sll 2`, `lw 0x49B4($at)` is the guest's
                own level-name pointer table at 0x800649B4 (entries 0x80066EA0 "Glimmer", 0x800106D0 "Idol
                Springs", 0x800106E0 "Summer Forest"); 0x80053D50..0x80053D74 reads the level id word
                0x80066F90 (`lw 0x6F90`) through the byte table at 0x80064940 to the same name table.
  Spyro 3 (SCUS_944.67) game-state word 0x8006E344
    0x80055420  lui $v1,0x8007 ; 0x80055424 lw $v1,-0x1CBC($v1)    the per-frame update's switch operand
                (0x8005542C `sltiu $v0,$v1,0x14` bounds it at 20 states)
    0x80011204  the 20-entry handler table it indexes; 0 = playing, 5 = loading, 6 = cutscene and 11 = title
                were OBSERVED live (state 0's entry 0x80055450 is the gameplay update)
    position triple 0x8006E020 (x, y, z): 0x80012354/0x80012358 `lw $v1,-0x1FD8($v1)` and 0x80012364/
                0x80012368 `sw $v1,-0x1FD8($at)` read-modify-write the z word 0x8006E028 (`+= v >> 6`);
                0x80012FC0/0x80012FC4 hands the triple's base, `addiu $a0,$a0,-0x1FE0`, to 0x8004F178.
                The word at +0x0C holds the previous position. Live: up moved y by +1201, a jump moved z
                by +451 and back.

REFUSALS. An unknown title name is refused with the names that exist; a profile whose three route states
are not distinct is refused at construction, because a route that cannot tell loading from playing would
report a capture of a loading screen as gameplay.
"""

from __future__ import annotations

import sys
from collections.abc import Mapping
from dataclasses import dataclass


class Refusal(RuntimeError):
    """The requested title or profile is not one this driver can play honestly."""


@dataclass(frozen=True)
class LevelFacts:
    """Where a title keeps the level it is in, and the guest's own name table for it.

    The name is NOT inferred from a picture: the guest's name table holds a pointer per level, indexed
    `(homeworld << 5) + level`, and the route reads the pointer and the string it names."""

    homeworld_word: int
    level_word: int
    level_id_word: int
    name_table: int

    def name_pointer_address(self, homeworld: int, level: int) -> int:
        return self.name_table + 4 * ((homeworld << 5) + level)


@dataclass(frozen=True)
class TitleProfile:
    label: str
    image: str
    disc_variable: str
    gamestate_word: int
    state_title: int
    state_loading: int
    state_playing: int
    # The first word of the player's (x, y, z) triple; y is +4 and z +8.
    position_word: int
    # The pad direction that walks away from the first conversation. Spyro 2's start faces a character who
    # opens a dialogue (state 1) within a 40-field run forward; walking back stays in the playing state.
    walk_button: str
    # The game state of a conversation, and the pad direction that walks into one. None when this title's
    # conversation has not been observed (tools/title_conversation.py refuses such a title by name).
    state_dialogue: int | None = None
    dialogue_button: str | None = None
    level: LevelFacts | None = None

    def __post_init__(self) -> None:
        states = {self.state_title, self.state_loading, self.state_playing}
        if len(states) != 3:
            raise Refusal(f"{self.label}: title, loading and playing must be three distinct game states")

    def position_words(self) -> tuple[int, int, int]:
        return self.position_word, self.position_word + 4, self.position_word + 8


TITLES: Mapping[str, TitleProfile] = {
    "spyro2": TitleProfile(
        label="Spyro 2 (SCUS_944.25)",
        image="scratch/assets/spyro2/SCUS_944.25",
        disc_variable="PSXPORT_SPYRO2_DISC",
        gamestate_word=0x800681C8,
        state_title=11,
        state_loading=5,
        state_playing=0,
        position_word=0x80067EE4,
        walk_button="down",
        state_dialogue=1,
        dialogue_button="up",
        level=LevelFacts(
            homeworld_word=0x80067118,
            level_word=0x80066F54,
            level_id_word=0x80066F90,
            name_table=0x800649B4,
        ),
    ),
    "spyro3": TitleProfile(
        label="Spyro 3 (SCUS_944.67)",
        image="scratch/assets/spyro3/SCUS_944.67",
        disc_variable="PSXPORT_SPYRO3_DISC",
        gamestate_word=0x8006E344,
        state_title=11,
        state_loading=5,
        state_playing=0,
        position_word=0x8006E020,
        walk_button="up",
    ),
}


def profile(name: str) -> TitleProfile:
    try:
        return TITLES[name]
    except KeyError:
        raise Refusal(f"unknown title {name!r}; the profiles are {', '.join(sorted(TITLES))}") from None


def _selftest() -> int:
    for name, entry in TITLES.items():
        assert profile(name) is entry
        x, y, z = entry.position_words()
        assert (y, z) == (x + 4, x + 8), name
    assert TITLES["spyro2"].gamestate_word != TITLES["spyro3"].gamestate_word
    for label, attempt in {
        "unknown title": lambda: profile("spyro9"),
        "indistinct route states": lambda: TitleProfile("x", "i", "V", 0x80000000, 5, 5, 0, 0x80000010, "up"),
    }.items():
        try:
            attempt()
        except Refusal as refusal:
            print(f"  refuses {label}: {refusal}")
        else:
            print(f"SELFTEST FAILED: {label} was accepted", file=sys.stderr)
            return 1
    print("title_profile selftest PASS")
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv[1:]:
        raise SystemExit(_selftest())
    for key, entry in TITLES.items():
        print(f"{key}: {entry}")
