#!/usr/bin/env python3
"""Compare the Spyro 1 Lightrec product against psxport's independent Beetle full-console
reference at title-owned state checkpoints (psxport docs/oracle.md, tools/oracle/compare.py).

    uv run --frozen python tools/oracle_compare.py --bios ../SCPH1001.BIN
    uv run --frozen python tools/oracle_compare.py --bios ../SCPH1001.BIN --selftest

The title policy lives in tools/oracle_spyro1.py; the product launch environment and disc come
from tools/drive.py so every agent driver of the product builds them in one place.
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

OUT_DIR = ROOT / "scratch" / "oracle"
DEFAULT_BIOS = ROOT.parent / "SCPH1001.BIN"


def main() -> int:
    parser = compare.build_parser(__doc__, DEFAULT_BIOS)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    args = parser.parse_args()
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc)
    environment.update(compare.product_env(args))
    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT, Path(disc))
    return compare.run(title, product, args, OUT_DIR)


if __name__ == "__main__":
    raise SystemExit(main())
