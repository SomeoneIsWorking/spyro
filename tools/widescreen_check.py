#!/usr/bin/env python3
"""Does Spyro 1's widescreen EXTEND the picture, or change it?

    uv run --frozen python tools/widescreen_check.py

Drives the product to one settled gameplay state twice -- once at 4:3, once at 16:9 -- and hands the
two captures to psxport's widescreen analyser, which owns the question because it is title-neutral.
This tool owns only how Spyro reaches a comparable state.

WHY THE STATE ORACLE DOES NOT ANSWER THIS. tools/oracle_compare.py reports 15/15 checkpoints
byte-identical with widescreen on, and would report exactly that for a port which stretched its 4:3
frame to fill the wider viewport: stretching writes no guest state. The picture oracle cannot answer
it either, because its reference is a console and a console is 4:3 -- there is nothing to compare
the extra area against. So the product is asked about itself.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import widescreen  # noqa: E402

OUT_DIR = ROOT / "scratch" / "widescreen"
# The same settled Artisans state tools/oracle_spyro1.py photographs at `settled_play`: past the
# level intro, no input held, so the two runs render one scene rather than two moments of a fade.
SETTLE = 180
ASPECTS = {"narrow": "aspect=0\nfps60=0\n", "wide": "aspect=1\nfps60=0\n"}


def capture(name: str, settings_body: str) -> Path:
    settings = OUT_DIR / f"{name}.ini"
    settings.write_text(settings_body)
    shot = OUT_DIR / f"{name}.png"
    log = OUT_DIR / f"{name}.log"
    command = [sys.executable, str(TOOLS / "drive.py"), "gameplay", "--skip-transitions",
               "--settle", str(SETTLE), "--settings", str(settings), "--log", str(log),
               "--shot", str(shot)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"REFUSED: the {name} run failed ({result.returncode}); nothing was measured\n"
                 f"{result.stdout}\n{result.stderr}")
    announced = [line for line in log.read_text(errors="replace").splitlines()
                 if "[wide] native picture:" in line]
    if not announced:
        sys.exit(f"REFUSED: the {name} run never announced its picture geometry, so there is no "
                 f"evidence it rendered at the requested aspect")
    print(f"[widescreen] {name}: {announced[-1].split('[wide] ')[-1]}")
    return shot


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    captures = {name: capture(name, body) for name, body in ASPECTS.items()}
    try:
        result = widescreen.analyse(captures["narrow"], captures["wide"], OUT_DIR)
    except widescreen.Unanswerable as refusal:
        print(f"[widescreen] REFUSED: {refusal}", file=sys.stderr)
        return 2
    widescreen.announce(result)
    report = OUT_DIR / "widescreen.json"
    report.write_text(json.dumps(result.report(), indent=2))
    print(f"[widescreen] report: {report}")
    print(f"[widescreen] the centre crop and its magnitude map are beside it; NEITHER NUMBER SAYS "
          f"THE EXTRA GEOMETRY IS CORRECT — no 16:9 reference exists to say that.")
    return 0 if result.extends else 1


if __name__ == "__main__":
    raise SystemExit(main())
