#!/usr/bin/env python3
"""Per-game-frame guest PHASE trace of the product against the full-console reference.

    uv run --frozen python tools/phase_trace.py --frames 2500
    uv run --frozen python tools/phase_trace.py --frames 2500 --cores product

WHY THIS EXISTS. `tools/oracle_compare.py` says a comparison DIVERGEd and prints the ranges that
differ. It cannot say WHERE the two cores stopped being at the same moment, because it drives both
to a checkpoint by a predicate and only reads the declared ranges on arrival. Two cores that agree at
every checkpoint and spend wildly different numbers of frames getting there produce the same report
as two that drift: both just arrive somewhere different. This tool samples a small fixed word list on
EVERY game frame of both cores and reports, per guest state, the game frame each core entered it on
and how many frames it spent there. The slip is then a number rather than an inference.

Both cores are the comparator's own sessions (`compare_cores.NativeReplSession`,
`compare_cores.ConsoleSession`) and one game frame is the title policy's own `advance`, so "frame" and
"core" mean exactly what they mean in the comparison. Nothing here is a new launch or stepping
policy.

WHAT IT DOES NOT SHOW. Sampled words are a census, not a comparison: a phase table is evidence about
HOW MANY frames each core spends in each state, and about the clocks it enters with. It does not
compare the whole of RAM and it cannot replace `tools/oracle_compare.py`, which does that at every
checkpoint with the declared ranges. Use this to localise a divergence, then the comparator to judge
it.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare
import compare_cores
import drive
import guest_globals

PSXPORT = ROOT / "external" / "psxport"
OUT_DIR = ROOT / "scratch" / "phase"
DEFAULT_BIOS = ROOT.parent / "SCPH1001.BIN"

# The census words. Every address is the shipping owner in game/core/guest_globals.h where there is
# one, the authenticated image's symbols otherwise; a word is here because a phase boundary is decided
# by it, not because it is interesting.
G_DEMO_MODE = 0x80075714        # g_DemoMode: where the pad comes from, and the demo's own gate
G_DEMO_FADE_TIMER = 0x80075884  # the demo's own end-of-demo counter
G_DEMO_DATA_PTR = 0x8007585C    # the walking read pointer into the level's recorded input
G_CUTSCENE_LAYOUT = 0x80075680  # g_CutsceneLayout, a POINTER; m_CurrentTick is its first member
G_PAD_BUFFER = 0x800786A0       # g_PadBuffer: status | size_type<<8 | inputs[0]<<16 | inputs[1]<<24
D_OCCLUSION_RESULT = 0x80075844

WORDS: dict[str, int] = {
    "gamestate": guest_globals.kGamestate,
    "game_tick": guest_globals.kGameTick,
    "level_id": guest_globals.kLevelId,
    "load_stage": guest_globals.kLoadStage,
    "level_ticks": guest_globals.kLevelTicks,
    "unprocessed": guest_globals.kUnprocessedFrames,
    "delta_time": guest_globals.kDeltaTime,
    "demo_mode": G_DEMO_MODE,
    "demo_fade": G_DEMO_FADE_TIMER,
    "demo_data": G_DEMO_DATA_PTR,
    "pad_buffer": G_PAD_BUFFER,
    "occlusion": D_OCCLUSION_RESULT,
}

# The title overlay's own six words, read as one block: m_Mode, m_State, m_Tick, m_SubTick,
# m_SubState, m_OptionSelected.
TITLE_WORDS = ("mode", "state", "tick", "sub_tick", "sub_state", "option")
TITLE_ADDRESS = guest_globals.kTitlescreenState

GS_PLAYING = 0
GS_TITLE_SCREEN = 13
TSM_INIT, TSM_MENU, TSM_LOADING, TSM_DEMO = 0, 1, 2, 3
TSS_SETUP, TSS_LOADING, TSS_ACTIVE = 0, 1, 2

GAMESTATE_NAMES = {0: "playing", 1: "level_transition", 9: "entrance_animation",
                   13: "title_screen", 14: "cutscene", 15: "credits"}


def _u32(raw: bytes, offset: int = 0) -> int:
    return int.from_bytes(raw[offset:offset + 4], "little")


class Sample:
    """One game frame of one core: the census words, the title overlay's six, and the flyby clock."""

    __slots__ = ("cutscene_tick", "frame", "title", "words")

    def __init__(self, frame: int, words: dict[str, int], title_state: tuple[int, ...], cutscene_tick: int):
        self.frame = frame
        self.words = words
        self.title = title_state
        self.cutscene_tick = cutscene_tick

    @property
    def gamestate(self) -> int:
        return self.words["gamestate"]

    def clock(self) -> str:
        """The clocks, as one comparable string: the title overlay's own tick, the flyby clock the
        title's animation is driven from, and the field counter. These three are what a phase spends
        its frames on, so they are what says `different rate` apart from `different start`."""
        return (f"title_tick={self.title[2]} sub_tick={self.title[3]} "
                f"flyby_tick={self.cutscene_tick} level_ticks={self.words['level_ticks']} "
                f"game_tick={self.words['game_tick']}")

    def phase(self) -> str:
        """The guest state this frame is in, named from the words that decide it. The demo states are
        split by the overlay's own TSS_* phases because the flyby (TSS_Active) is the long one and the
        load (TSS_Loading) is a blocking loop whose length is the interesting part."""
        mode, state, sub_state = (self.title[index] for index in (0, 1, 4))
        gamestate = GAMESTATE_NAMES.get(self.gamestate, f"gs{self.gamestate}")
        if self.gamestate == GS_PLAYING:
            return f"{gamestate}/level{self.words['level_id']}" + (
                f"/demo{self.words['demo_mode']}" if self.words["demo_mode"] else "")
        if mode == TSM_DEMO:
            return f"{gamestate}/demo/{('setup', 'loading', 'flyby')[min(state, 2)]}"
        if mode == TSM_INIT:
            return f"{gamestate}/init/{sub_state}"
        return f"{gamestate}/mode{mode}/state{state}"

    def detail(self) -> str:
        """The demo's own decision words, decoded. `func_800334D4` ends the demo when
        `g_GameTick >= g_DemoLengths[g_DemoIndex] - 8`, or when `g_GameTick >= 16` and the pad buffer
        reports a connected controller (`status == 0`) with a button that is not released
        (`inputs[i] != 0xFF`) -- so the button test is only meaningful next to those two bytes, and
        `demo_data` advancing is what says the recorded stream is actually being played."""
        pad = self.words["pad_buffer"]
        return (f"demo_mode={self.words['demo_mode']} fade={self.words['demo_fade']} "
                f"game_tick={self.words['game_tick']} pad.status={pad & 0xFF} "
                f"pad.type={(pad >> 8) & 0xFF} pad.buttons=0x{(pad >> 16) & 0xFFFF:04X} "
                f"demo_data=0x{self.words['demo_data']:08X}")

class PhaseTrace:
    """Per-core phase census: the game frame each state was first seen on, how many frames it was
    spent in, the clocks it was entered with, and -- the number that localises a pacing divergence --
    how many FIELDS each of those game frames cost.

    A game frame is the title policy's `advance`, which is the framework's `step_until_counter_resets`
    over the guest's per-field counter: one main-loop iteration. It is not one field. `steps` is the
    step count that same barrier returns, and `fields` is the guest's own field counter across the
    phase, so `fields / advances` is fields-per-game-frame and a phase where the two cores disagree on
    it is a phase where one core's main loop is spinning and the other's is paced."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.first: dict[str, int] = {}
        self.frames: dict[str, int] = {}
        self.steps: dict[str, int] = {}
        self.fields: dict[str, int] = {}
        self.measured: dict[str, int] = {}
        self.widest: dict[str, tuple[int, int]] = {}
        self.entry_clock: dict[str, str] = {}
        self.samples = 0
        self._last_fields: int | None = None

    def record(self, sample: Sample, steps: int) -> None:
        phase = sample.phase()
        self.samples += 1
        if phase not in self.first:
            self.first[phase] = sample.frame
            self.entry_clock[phase] = sample.clock()
        self.frames[phase] = self.frames.get(phase, 0) + 1
        self.steps[phase] = self.steps.get(phase, 0) + steps
        if self._last_fields is not None:
            # Fields are counted as the guest's own counter moves between two samples, so the FIRST
            # sample of a run has no predecessor and its fields are unknown rather than zero. A phase
            # whose only frame is that one therefore has no rate, and printing 0.0 for it would read
            # as "this phase costs no fields".
            self.fields[phase] = self.fields.get(phase, 0) + max(
                0, sample.words["level_ticks"] - self._last_fields)
            self.measured[phase] = self.measured.get(phase, 0) + 1
        self._last_fields = sample.words["level_ticks"]
        widest = self.widest.get(phase, (0, 0))[0]
        if steps > widest:
            self.widest[phase] = (steps, sample.frame)


def read_sample(core: compare_cores.CoreSession, frame: int) -> Sample:
    words = {name: _u32(core.read(address, 4)) for name, address in WORDS.items()}
    title_state = tuple(_u32(core.read(TITLE_ADDRESS + 4 * index, 4)) for index in range(len(TITLE_WORDS)))
    # g_CutsceneLayout is a pointer; the flyby clock is its first member, and reading through it is
    # the only way to see the animation phase the title screen spends its frames in.
    layout = _u32(core.read(G_CUTSCENE_LAYOUT, 4))
    cutscene_tick = _u32(core.read(layout, 4)) if 0x80000000 <= layout < 0x80080000 else -1
    return Sample(frame, words, title_state, cutscene_tick)


def trace(core: compare_cores.CoreSession, frames: int, census: PhaseTrace,
          follow: str | None = None) -> Sample | None:
    """Advance `core` one game frame at a time and census it. The barrier is the framework's own
    `step_until_counter_resets`, which is exactly what the title policy's `advance` calls, so a game
    frame here means what it means in the comparison; this keeps the step count it returns, which
    `advance` discards. `follow` prints one row per frame whose phase label contains it, which is how
    a phase that ends early is read rather than guessed at. Returns the last sample."""
    last: Sample | None = None
    for frame in range(1, frames + 1):
        steps = compare.step_until_counter_resets(core, guest_globals.kUnprocessedFrames)
        last = read_sample(core, frame)
        census.record(last, steps)
        if follow is not None and follow in last.phase():
            print(f"[frame] {census.name} f{frame} steps={steps} {last.phase()} {last.detail()}")
    return last


def report(traces: list[PhaseTrace], frames: int, seconds: float) -> None:
    names = [census.name for census in traces]
    phases: list[str] = []
    for census in traces:
        for phase in census.first:
            if phase not in phases:
                phases.append(phase)

    width = max((len(phase) for phase in phases), default=10)
    print(f"[phase] {frames} game frames, {seconds:.1f}s, one game frame per core per step")
    print(f"{'phase':<{width}}  " + "  ".join(f"{name:>34}" for name in names))
    print(f"{'':<{width}}  " + "  ".join(f"{'entered  frames  fields/frame  widest':>34}" for _ in names))
    for phase in phases:
        cells = []
        for census in traces:
            if phase in census.first:
                measured = census.measured.get(phase, 0)
                rate = (f"{census.fields[phase] / measured:.1f}" if measured else "n/a")
                steps, at = census.widest[phase]
                cells.append(f"{census.first[phase]:>6} {census.frames[phase]:>7} {rate:>13}  "
                             f"{steps:>5} at f{at}")
            else:
                cells.append(f"{'not reached':>34}")
        print(f"{phase:<{width}}  " + "  ".join(f"{cell:>34}" for cell in cells))
    print()
    for census in traces:
        counted = sum(census.frames.values())
        print(f"[phase] {census.name}: {counted} frames censused over {len(census.first)} state(s) "
              f"({sum(census.steps.values())} barrier steps, {sum(census.fields.values())} guest fields)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=2500, help="game frames per core (default 2500)")
    parser.add_argument("--cores", choices=("both", "product", "console"), default="both")
    parser.add_argument("--follow", metavar="PHASE",
                        help="print one row per frame whose phase label contains this, with the demo's "
                             "own decision words, so a phase that ends early can be read")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=DEFAULT_BIOS)
    args = parser.parse_args()

    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    for path in (ROOT / args.executable, ROOT / args.binary, args.bios):
        if not path.is_file():
            print(f"REFUSED: {path} is missing; build the product and provision the title first",
                  file=sys.stderr)
            return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    sessions: list[compare_cores.CoreSession] = []
    if args.cores in ("both", "product"):
        environment = drive.environment(disc)
        # NativeReplSession runs [program, game image]: the port executable first, the authenticated
        # executable second, which is the order compare.Product carries them in.
        sessions.append(compare_cores.NativeReplSession(
            str(ROOT / args.executable), str(ROOT / args.binary), environment, str(ROOT),
            OUT_DIR / "product.log"))
    if args.cores in ("both", "console"):
        sessions.append(compare_cores.ConsoleSession(
            PSXPORT, Path(disc), args.bios, "na", OUT_DIR / "console.log"))
    try:
        censuses = [PhaseTrace(core.name) for core in sessions]
        for core, census in zip(sessions, censuses, strict=True):
            trace(core, args.frames, census, args.follow)
        report(censuses, args.frames, time.monotonic() - started)
    finally:
        for core in sessions:
            core.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
