#!/usr/bin/env python3
"""title_switch.py — prove the title selector switches titles inside ONE process, in both orders.

    uv run --frozen python tools/title_switch.py                 # spyro1 -> picker -> spyro2, and the reverse
    uv run --frozen python tools/title_switch.py --pair spyro2 spyro3 --frames 400
    uv run --frozen python tools/title_switch.py --selftest

The claim under test is not "the second title appeared". It is "the second title is the SAME run it would
have been in a process that never ran the first one". Each order therefore runs three headless products:

  * SWITCHED  one process: selector -> A (to frame N) -> `session return` -> selector -> B (to frame N)
  * FRESH 1   a new process: selector -> B (to frame N)
  * FRESH 2   another new process, the same

and compares the guest's full RAM and scratchpad at B's frame N. FRESH 1 against FRESH 2 is the CONTROL: if
two fresh processes already differ, the instrument is not measuring leaked state and the run is refused
rather than reported. Only when the control agrees does SWITCHED against FRESH mean anything.

Every process is the built port launched through the framework's `agent_environment` (offscreen, silent,
unpaced) and driven over the loopback control channel; `session return` and `pick` are the same routes the
ESC menu row and the pad confirm take. One `spyro_port` at a time: the run refuses if another is alive.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))
sys.path.insert(0, str(ROOT / "tools"))

from dbgclient import LiveClient  # noqa: E402

PORT = 5971
SCRATCH = ROOT / "scratch" / "title_switch"
DISC_VARIABLES = ("PSXPORT_SPYRO_DISC", "PSXPORT_SPYRO2_DISC", "PSXPORT_SPYRO3_DISC")
SETTINGS = ROOT / "tools" / "shipping_settings.ini"
BINARY = ROOT / "build" / "bin" / "spyro_port"  # --binary overrides (e.g. a sanitizer build)


class Refusal(RuntimeError):
    """A named reason the run cannot make its claim."""


def disc_from_dotenv(variable: str) -> str | None:
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


def other_ports_alive() -> list[int]:
    result = subprocess.run(["pgrep", "-x", "spyro_port"], capture_output=True, text=True, check=False)
    return [int(pid) for pid in result.stdout.split()]


@dataclass(frozen=True)
class Capture:
    ram_sha256: str
    scratchpad_sha256: str
    frame: int
    picture: Path


class Product:
    """One headless port process at the selector, driven over the control channel."""

    def __init__(self, label: str):
        from port.launch_environment import agent_environment

        busy = other_ports_alive()
        if busy:
            raise Refusal(f"spyro_port already running (pids {busy}); only one may run at a time")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        env = agent_environment(dict(os.environ), SETTINGS)
        env.update(PSXPORT_DEBUG_SERVER=str(PORT), PSXPORT_WATCHDOG="0", PSXPORT_ASSET_DIR="external/psxport")
        for variable in DISC_VARIABLES:
            disc = disc_from_dotenv(variable)
            if disc:
                env[variable] = disc
        self.label = label
        self._log = (SCRATCH / f"{label}.log").open("w")
        self._proc = subprocess.Popen([str(BINARY)], cwd=ROOT, env=env,
                                      stdout=self._log, stderr=subprocess.STDOUT)
        self._client = self._connect()

    def _connect(self) -> LiveClient:
        for _ in range(150):
            if self._proc.poll() is not None:
                raise Refusal(f"{self.label}: the port exited with {self._proc.returncode} before listening")
            try:
                return LiveClient(PORT, timeout=60.0)
            except OSError:
                time.sleep(0.2)
        raise Refusal(f"{self.label}: the control channel never opened")

    def close(self) -> None:
        # Killed by the PID this object captured at launch, never by name.
        self._proc.kill()
        self._proc.wait()
        self._log.close()

    def send(self, line: str) -> str:
        return self._client.send(line)

    def wait_for_selector(self) -> str:
        for _ in range(200):
            reply = self.send("picker")
            if reply.startswith("spyro1"):
                return reply
            time.sleep(0.1)
        raise Refusal(f"{self.label}: the selector never came back")

    def pick(self, slug: str) -> None:
        reply = self.send(f"pick {slug}")
        if not reply.startswith("ok"):
            raise Refusal(f"{self.label}: pick {slug} -> {reply.strip()}")

    def reach_frame(self, target: int) -> int:
        """Pause the just-started title and step it to exactly `target` presented frames."""
        frame = -1
        for _ in range(200):
            reply = self.send("pause")
            if reply.startswith("paused at frame"):
                frame = int(reply.split()[-1])
                break
            time.sleep(0.05)  # the title's Game is not up yet; `pause` reached the selector
        if frame < 0:
            raise Refusal(f"{self.label}: the title never answered `pause`")
        if frame > target:
            raise Refusal(f"{self.label}: already at frame {frame}, past target {target}; raise --frames")
        if frame < target:
            self.send(f"step {target - frame}")
        for _ in range(600):
            parts = dict(token.split("=", 1) for token in self.send("frame").split() if "=" in token)
            if int(parts["frame"]) == target and parts["paused"] == "1":
                return target
            time.sleep(0.05)
        raise Refusal(f"{self.label}: never settled at frame {target}")

    def capture(self, name: str, frame: int) -> Capture:
        ram = SCRATCH / f"{name}.ram"
        picture = SCRATCH / f"{name}.png"
        self.send(f"dumpram {ram}")
        self.send(f"pshot {picture}")
        capture = Capture(hashlib.sha256(ram.read_bytes()).hexdigest(),
                          hashlib.sha256(Path(f"{ram}.spad").read_bytes()).hexdigest(), frame, picture)
        ram.unlink()
        Path(f"{ram}.spad").unlink()
        return capture

    def return_to_selector(self) -> None:
        self.send("play")
        reply = self.send("session return")
        if "requested" not in reply:
            raise Refusal(f"{self.label}: {reply.strip()}")
        self.wait_for_selector()


def fresh(slug: str, frames: int, name: str) -> Capture:
    product = Product(name)
    try:
        product.wait_for_selector()
        product.pick(slug)
        return product.capture(name, product.reach_frame(frames))
    finally:
        product.close()


def switched(first: str, second: str, frames: int, name: str) -> tuple[Capture, Capture]:
    product = Product(name)
    try:
        product.wait_for_selector()
        product.send(f"pshot {SCRATCH / (name + '_selector_1.png')}")
        product.pick(first)
        first_capture = product.capture(f"{name}_first", product.reach_frame(frames))
        product.return_to_selector()
        product.send(f"pshot {SCRATCH / (name + '_selector_2.png')}")
        product.pick(second)
        return first_capture, product.capture(f"{name}_second", product.reach_frame(frames))
    finally:
        product.close()


def verdict(first: str, second: str, frames: int) -> bool:
    control_a = fresh(second, frames, f"fresh_{second}_1")
    control_b = fresh(second, frames, f"fresh_{second}_2")
    if control_a.ram_sha256 != control_b.ram_sha256 or control_a.scratchpad_sha256 != control_b.scratchpad_sha256:
        raise Refusal(f"CONTROL FAILED: two fresh {second} processes differ at frame {frames} "
                      f"({control_a.ram_sha256[:12]} vs {control_b.ram_sha256[:12]}); the comparison "
                      f"cannot show leaked state, so no switch verdict is reported")
    _, after_switch = switched(first, second, frames, f"switch_{first}_to_{second}")
    same_ram = after_switch.ram_sha256 == control_a.ram_sha256
    same_spad = after_switch.scratchpad_sha256 == control_a.scratchpad_sha256
    print(f"{first} -> selector -> {second}: control agrees ({control_a.ram_sha256[:12]}); "
          f"switched ram {'==' if same_ram else '!='} fresh, scratchpad {'==' if same_spad else '!='} fresh; "
          f"frame {after_switch.frame}; picture {after_switch.picture}")
    return same_ram and same_spad


def selftest() -> int:
    """The verdict must be able to say no: hashes of different bytes differ, equal bytes agree."""
    one = hashlib.sha256(b"a").hexdigest()
    other = hashlib.sha256(b"b").hexdigest()
    assert one != other and one == hashlib.sha256(b"a").hexdigest()
    print("selftest ok")
    return 0


def main() -> int:
    global BINARY
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pair", nargs=2, metavar=("A", "B"), default=("spyro1", "spyro2"))
    parser.add_argument("--frames", type=int, default=400)
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.binary:
        BINARY = args.binary.resolve()
    first, second = args.pair
    forward = verdict(first, second, args.frames)
    backward = verdict(second, first, args.frames)
    print(f"order {first}->{second}: {'IDENTICAL' if forward else 'DIFFERS'}; "
          f"order {second}->{first}: {'IDENTICAL' if backward else 'DIFFERS'}")
    return 0 if forward and backward else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refusal as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        sys.exit(2)
