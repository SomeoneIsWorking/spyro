#!/usr/bin/env python3
"""live_play.py — PLAY the port over its live debug server and report what it did.

    uv run --frozen python tools/live_play.py
    uv run --frozen python tools/live_play.py --hold left --seconds 20 --port 5971

WHY THIS IS NOT `tools/drive.py`. `drive.py` blocks the product between commands over the framework
REPL, which is the right tool for stepping to a state and reading it. This one leaves the product
running at full speed and talks to it the way a player would be watched: a pad edge delivered across
real presented frames, guest state sampled while the picture keeps updating, and screenshots taken of
what is actually on screen. That is the only way to see a defect that needs the game to be RUNNING —
a producer that refuses after 20 seconds of play, a picture that goes black on the way to a portal, a
frame-time cliff — rather than a state that happens to be parked at a barrier.

The menu sequence is NOT written here: `tools/title_prompts.py` owns which button a screen is asking
for, and `tools/drive.py` asks the same function, so the two drivers cannot answer different prompts.

The run is headless, silent and unpaced (the framework's `agent_environment`, via `drive.py`), and it
kills the product by the PID it launched. It is still a real product run: no interpreter, no
fabricated state, and every number it prints came out of the running game.
"""

from __future__ import annotations

import argparse
import signal
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))

import drive
import guest_globals
import title_prompts
from dbgclient import (
    LiveClient,
)

G_LEVEL_TRANS_HUD = drive.G_LEVEL_TRANS_HUD
OUT_DIR = ROOT / "scratch" / "live"
OBSERVE_FRAMES = 20  # frames between decisions, as drive.py's Navigator uses


def launch(executable: Path, binary: Path, disc: str | None, port: int, log: Path) -> subprocess.Popen:
    """The product, headless and silent, with the live endpoint on its own port. PSXPORT_REPL is popped
    because nothing here speaks that protocol, and a product waiting for a prompt on stdin would never
    start presenting."""
    environment = drive.environment(disc)
    environment.pop("PSXPORT_REPL", None)
    environment["PSXPORT_DEBUG_SERVER"] = str(port)
    environment["PSXPORT_LOG_FILE"] = str(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text("")
    return subprocess.Popen([str(executable), str(binary)], cwd=ROOT, env=environment,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def connect(port: int, seconds: float) -> LiveClient:
    """Wait for the endpoint, and REFUSE with the product's own log if it never answers. A client that
    cannot connect must not fall back to anything else: 'no live session' is the finding, and a run
    that quietly used another path would report play evidence it does not have."""
    deadline = time.monotonic() + seconds
    last: OSError | None = None
    while time.monotonic() < deadline:
        try:
            return LiveClient(port)
        except OSError as error:
            last = error
            time.sleep(0.5)
    raise SystemExit(f"REFUSED: the live endpoint never answered on 127.0.0.1:{port} within {seconds:.0f}s "
                     f"({last}); see the run's log")


class Session:
    """The play-through: reach gameplay the way a player does, then hold what it is told to and report
    what it saw. Every step counts what it did, so a run that never got past the title says so with a
    number rather than a zero."""

    def __init__(self, client: LiveClient, log: Path) -> None:
        self.client = client
        self.log = log
        self.presses = 0
        self.first_frame = client.frame()

    def screen(self) -> title_prompts.Screen:
        return title_prompts.Screen(
            gamestate=self.client.word(guest_globals.kGamestate),
            title=drive.TitleState(*self.client.words(guest_globals.kTitlescreenState, 6)),
            level_trans_hud=self.client.word(G_LEVEL_TRANS_HUD))

    def answer(self, prompt: title_prompts.Prompt) -> bool:
        if prompt.refuse:
            raise SystemExit(f"REFUSED: {prompt.refuse} at {self.screen()}")
        if not prompt.buttons:
            return False
        for button in prompt.buttons:
            self.client.tap(button)
            self.presses += 1
        return True

    def advance(self, frames: int = OBSERVE_FRAMES) -> int:
        """Wait for `frames` presented frames. `pause`/`step` would make this exact, but the point of
        this tool is a RUNNING game, so it reads the product's own frame counter instead of ordering
        the product to do anything."""
        target = self.client.frame() + frames
        while self.client.frame() < target:
            time.sleep(0.01)
        return self.client.frame()

    def drive_to_gameplay(self, budget_frames: int) -> int:
        """Boot -> title -> save picker -> playable level, in the three legs title_prompts owns."""
        legs = (("save picker", title_prompts.title_menu_prompt),
                ("new game", title_prompts.new_game_prompt),
                ("GS_Playing", title_prompts.load_route_prompt))
        spent = 0
        leg = 0
        while spent < budget_frames and leg < len(legs):
            name, decide = legs[leg]
            screen = self.screen()
            if decide(screen).reached:
                print(f"[live] {name} reached at {screen.gamestate} "
                      f"title={screen.title.mode}/{screen.title.state} frame={self.advance(0)}")
                leg += 1
                continue
            self.answer(decide(screen))
            self.advance()
            spent += OBSERVE_FRAMES
        if leg < len(legs):
            raise SystemExit(f"REFUSED: reached only {leg} of {len(legs)} route legs "
                             f"({', '.join(name for name, _ in legs[:leg])}) within {budget_frames} frames; "
                             f"last {self.screen()}")
        return spent

    def hold(self, buttons: tuple[str, ...], seconds: float) -> dict:
        """Hold buttons for a wall-clock window while the game runs, and report what moved. Held input
        is what a player does; a tap is a menu answer."""
        for button in buttons:
            self.client.press(button)
        before = self.client.frame()
        started = time.monotonic()
        position_before = self.client.words(guest_globals.kSpyro, 3)
        ticks_before = self.client.word(guest_globals.kGameTick)
        while time.monotonic() - started < seconds:
            self.advance(OBSERVE_FRAMES)
        for button in buttons:
            self.client.release(button)
        after = self.client.frame()
        return {
            "buttons": list(buttons),
            "seconds": round(time.monotonic() - started, 1),
            "frames": after - before,
            "game_tick": self.client.word(guest_globals.kGameTick) - ticks_before,
            "player_before": position_before,
            "player_after": self.client.words(guest_globals.kSpyro, 3),
        }


def effective_configuration(client: LiveClient) -> dict:
    """The configuration the product is actually running, asked of the configuration owner over the
    endpoint. This is a QUERY, not a log read: the boot log prints each knob once, at boot, and the
    picture's mode changes afterwards, so a log line is a statement about the past and this is the
    answer as it stands. `cvars` also carries the layer each value came from, which is what says
    whether a run tested what it meant to test."""
    reply = client.send("cvars")
    knobs: dict[str, str] = {}
    layers: dict[str, str] = {}
    for line in reply.splitlines():
        fields = line.split()
        # "  NAME  kind = value [layer]" — the name is the first token, the layer the bracketed one.
        if len(fields) < 6 or fields[2] != "=" or not fields[-1].startswith("["):
            continue
        knobs[fields[0]] = fields[3]
        layers[fields[0]] = fields[-1].strip("[]")
    audit = next((line for line in reply.splitlines() if line.startswith("env audit:")), "")
    return {"knobs": knobs, "layers": layers, "env_audit": audit.strip(),
            "unmatched": sorted({line.split("UNKNOWN ")[1].split(" ")[0]
                                 for line in reply.splitlines() if line.strip().startswith("UNKNOWN ")})}


def guest_execution(client: LiveClient) -> dict:
    """The dynarec's own denominators for this run, live. A gameplay claim needs nonzero translated and
    executed blocks with the fallback accounted for; these are the numbers that say so, asked of the
    process rather than recovered from text it happened to print.

    The reply carries TWO lines, `guest:` and `fallback:`, and BOTH name `calls` and `instructions`.
    They are kept apart by their prefix rather than merged into one dict, because a merged dict
    answers both questions with whichever line was parsed last: on a run with zero fallback, the
    merged reading of `instructions` is the 202 million the DYNAREC executed.
    """
    numbers: dict[str, dict[str, int]] = {}
    for line in client.send("guest").splitlines():
        prefix, separator, rest = line.partition(":")
        prefix = prefix.strip()
        if not separator or prefix not in ("guest", "fallback"):
            continue
        numbers.setdefault(prefix, {})
        for token in rest.split():
            key, equals, value = token.partition("=")
            if equals:
                numbers[prefix][key] = int(value) if value.isdigit() else -1
    return numbers


def report_run(client: LiveClient) -> None:
    config = effective_configuration(client)
    knobs = config["knobs"]
    interesting = ("PSXPORT_WIDE", "PSXPORT_FPS60", "PSXPORT_RENDER_PATH", "PSXPORT_FRAMES",
                   "PSXPORT_IRES", "PSXPORT_SETTINGS", "PSXPORT_DEBUG_SERVER")
    print("[live] configuration, asked of the product:")
    for name in interesting:
        if name in knobs:
            print(f"[live]   {name} = {knobs[name]} [{config['layers'][name]}]")
    if config["unmatched"]:
        print(f"[live]   env variables that matched no knob: {config['unmatched']}")

    counters = guest_execution(client)
    dynarec = counters.get("guest", {})
    fallback = counters.get("fallback", {})
    executed = dynarec.get("executed_blocks", 0)
    print(f"[live] guest execution: {executed} executed blocks, "
          f"{dynarec.get('executed_instructions', 0)} instructions, "
          f"{dynarec.get('translated_blocks', 0)} translated, "
          f"{dynarec.get('cache_hits', 0)} cache hits / {dynarec.get('cache_misses', 0)} misses, "
          f"{dynarec.get('invalidations', 0)} invalidations, {dynarec.get('faults', 0)} faults")
    print(f"[live] interpreter fallback: {fallback.get('calls', -1)} calls, "
          f"{fallback.get('instructions', -1)} instructions, "
          f"{fallback.get('refused_calls', -1)} refused")
    if executed <= 0:
        raise SystemExit("REFUSED: the product presented frames but executed no guest blocks; "
                         "a play-through with no dynarec execution is not gameplay evidence")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--port", type=int, default=5971, help="live endpoint port (default 5971)")
    parser.add_argument("--hold", nargs="*", default=["left"], metavar="BUTTON",
                        help="buttons to hold once gameplay is reached (default: left)")
    parser.add_argument("--seconds", type=float, default=15.0, help="how long to hold them")
    parser.add_argument("--budget", type=int, default=12000, help="frames allowed to reach gameplay")
    parser.add_argument("--connect-timeout", type=float, default=180.0)
    parser.add_argument("--shot", type=Path, default=OUT_DIR / "gameplay.ppm")
    args = parser.parse_args()

    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    for path in (ROOT / args.executable, ROOT / args.binary):
        if not path.is_file():
            print(f"REFUSED: {path} is missing; build the product and provision the title first",
                  file=sys.stderr)
            return 2

    log = OUT_DIR / "live_play.log"
    product = launch(ROOT / args.executable, ROOT / args.binary, disc, args.port, log)
    client = None
    try:
        client = connect(args.port, args.connect_timeout)
        session = Session(client, log)
        print(f"[live] endpoint on 127.0.0.1:{args.port}, product pid {product.pid}, "
              f"presenting from frame {session.first_frame}")
        route_frames = session.drive_to_gameplay(args.budget)
        print(f"[live] gameplay reached after {route_frames} frames and {session.presses} menu answers")

        args.shot.parent.mkdir(parents=True, exist_ok=True)
        shot_reply = session.client.shot(str(args.shot)).strip()
        shot_bytes = args.shot.stat().st_size if args.shot.is_file() else 0
        print(f"[live] shot: {shot_reply} ({shot_bytes} bytes)")

        held = session.hold(tuple(args.hold), args.seconds) if args.hold else {}
        if held:
            moved = held["player_before"] != held["player_after"]
            print(f"[live] held {held['buttons']} for {held['seconds']}s: {held['frames']} presented "
                  f"frames, {held['game_tick']} game ticks, player {held['player_before']} -> "
                  f"{held['player_after']} ({'MOVED' if moved else 'did not move'})")

        report_run(client)
        session.client.quit()
        client = None
        return 0
    finally:
        if client is not None:
            client.close()
        # Kill by the PID this tool launched, never by name: another agent's product run must not die
        # with this one.
        if product.poll() is None:
            product.send_signal(signal.SIGTERM)
            try:
                product.wait(timeout=30)
            except subprocess.TimeoutExpired:
                product.kill()
                product.wait(timeout=30)
        print(f"[live] product pid {product.pid} exited {product.returncode}")


if __name__ == "__main__":
    raise SystemExit(main())
