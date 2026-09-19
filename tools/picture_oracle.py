#!/usr/bin/env python3
"""Compare the PICTURE the Spyro 1 product presents against psxport's independent Beetle
full-console reference, at the same title-owned states tools/oracle_compare.py compares RAM at.

    uv run --frozen python tools/picture_oracle.py --bios ../SCPH1001.BIN
    uv run --frozen python tools/picture_oracle.py --bios ../SCPH1001.BIN --selftest

tools/oracle_compare.py answers "does the simulation still behave"; it reads guest RAM and is
structurally blind to a rendering defect — a producer that draws nothing writes no different guest
state, so a missing layer and a perfect one report the same zero divergences. This answers "does it
still LOOK like the game", which is the question a player is actually asking.

That blindness is not hypothetical. On Tomba! 2 the RAM oracle reported 0 divergences over the exact
route on which the user could see the save menu was wrong; the picture oracle found the missing
button glyphs on its first run, and chasing them found a whole chrome producer that had never been
installed (Tomba2Engine issues 0013-0015). Spyro's RAM route is equally clean and equally blind.

Title policy is shared with the RAM comparison (tools/oracle_spyro1.py) and the launch environment
with every other agent driver (tools/drive.py), so there is one definition of each.
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare  # noqa: E402
import drive  # noqa: E402
import oracle_spyro1 as title  # noqa: E402
import picture  # noqa: E402

OUT_DIR = ROOT / "scratch" / "picture"
DEFAULT_BIOS = ROOT.parent / "SCPH1001.BIN"
# Why this run does not use tools/shipping_settings.ini: see the file's own header.
REFERENCE_SETTINGS = TOOLS / "reference_settings.ini"


def main() -> int:
    parser = picture.picture_parser(__doc__, DEFAULT_BIOS)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    args = parser.parse_args()
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc, REFERENCE_SETTINGS)
    environment.update(compare.product_env(args))
    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT, Path(disc))
    return picture.run(title, product, args, OUT_DIR)


if __name__ == "__main__":
    raise SystemExit(main())
