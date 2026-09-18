#!/usr/bin/env python3
"""guest_globals.py — the Spyro 1 guest addresses, read from the C++ owner rather than restated.

WHY THIS EXISTS. `game/core/guest_globals.h` names each shared retail global once for the shipping
product. The agent drivers in this directory need the same numbers, and a hand-written second copy
is exactly how a shipping owner and the oracle that checks it end up reading different memory — the
failure that header was created to end. Eighteen files spelled g_Camera before it existed; a Python
copy would simply be the nineteenth, in a language where nothing would ever compile it against the
other eighteen.

WHAT A NEGATIVE LOOKS LIKE. An initialiser this cannot evaluate is REFUSED by name, with the line
it came from. It is never skipped: a dropped constant would surface much later as a missing
attribute far from its cause, and would be indistinguishable from the header not defining it at
all. A header that parses to nothing is refused for the same reason.

    uv run --frozen python tools/guest_globals.py            # print what the header defines
    uv run --frozen python tools/guest_globals.py --selftest # prove the parser and its refusals
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = (ROOT / "game" / "core" / "guest_gp.h", ROOT / "game" / "core" / "guest_globals.h")

# `[inline] constexpr [std::]uint32_t kName = <initialiser>;`, which is the only form the owner uses.
DEFINITION = re.compile(
    r"^\s*(?:inline\s+)?constexpr\s+(?:std::)?uint32_t\s+(?P<name>k[A-Za-z0-9_]+)\s*=\s*(?P<value>[^;]+);"
)
HEX = re.compile(r"^0[xX][0-9a-fA-F]+[uU]?$")
SUM = re.compile(r"^(?P<base>k[A-Za-z0-9_]+)\s*\+\s*(?P<offset>0[xX][0-9a-fA-F]+[uU]?)$")


class Refusal(RuntimeError):
    """The header said something this parser will not guess at."""


def _term(text: str, known: dict[str, int], where: str) -> int:
    if HEX.match(text):
        return int(text.rstrip("uU"), 16)
    match = SUM.match(text)
    if match and match.group("base") in known:
        return known[match.group("base")] + int(match.group("offset").rstrip("uU"), 16)
    raise Refusal(f"{where}: cannot evaluate the initialiser {text!r}")


def parse(sources: dict[str, str]) -> dict[str, int]:
    """Every `constexpr uint32_t k…` the given headers define, in the order they define them."""
    known: dict[str, int] = {}
    for label, text in sources.items():
        for number, line in enumerate(text.splitlines(), start=1):
            match = DEFINITION.match(line)
            if not match:
                continue
            name = match.group("name")
            value = _term(match.group("value").split("//")[0].strip(), known, f"{label}:{number}")
            if known.get(name, value) != value:
                raise Refusal(f"{label}:{number}: {name} is already 0x{known[name]:08X} here")
            known[name] = value
    if not known:
        raise Refusal(f"no constants in {', '.join(sources)}; the owner's form must have changed")
    return known


def load(headers=HEADERS) -> dict[str, int]:
    missing = [str(path) for path in headers if not path.is_file()]
    if missing:
        raise Refusal(f"missing guest-address owner(s): {', '.join(missing)}")
    return parse({path.name: path.read_text() for path in headers})


_ADDRESSES = load()
globals().update(_ADDRESSES)
__all__ = sorted(_ADDRESSES)


def _selftest() -> int:
    """Both answers: a header the parser must read, and three it must refuse."""
    good = parse({"fixture.h": "inline constexpr std::uint32_t kBase = 0x80000010u;\n"
                               "constexpr uint32_t kField = kBase + 0x4u; // a comment\n"})
    assert good == {"kBase": 0x80000010, "kField": 0x80000014}, good
    for label, source in {
        "unevaluable initialiser": "constexpr uint32_t kThing = someCall(3);\n",
        "unknown base": "constexpr uint32_t kThing = kNeverDefined + 0x4u;\n",
        "nothing at all": "// only a comment\n",
    }.items():
        try:
            parse({"fixture.h": source})
        except Refusal as refusal:
            print(f"  refuses {label}: {refusal}")
        else:
            print(f"SELFTEST FAILED: {label} was accepted", file=sys.stderr)
            return 1
    print(f"  reads {len(_ADDRESSES)} constant(s) from the shipping owner")
    print("guest_globals selftest PASS")
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv[1:]:
        raise SystemExit(_selftest())
    for name, value in _ADDRESSES.items():
        print(f"{name:34} 0x{value:08X}")
