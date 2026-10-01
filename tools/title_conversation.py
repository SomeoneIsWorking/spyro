#!/usr/bin/env python3
"""title_conversation.py — walk into a Spyro 2 conversation and prove its text box draws glyphs.

    uv run --frozen python tools/title_conversation.py --title spyro2 --shot-dir scratch/play/spyro2-talk
    uv run --frozen python tools/title_conversation.py --selftest

WHY THIS EXISTS. `title_route.py` walks AWAY from the first conversation because an early capture of it
showed dark rounded rectangles and no text. The cause was not a missing text path: the guest reveals a
conversation's words one character at a time after the box opens (and the box shrinks away empty at the
end), so a capture taken while the box is opening or closing has a box and no glyphs, and the same run
captured later shows the whole speech. This route makes that a measured fact instead of an impression:
it walks forward until the guest's own game state says "conversation", then photographs the box every
SAMPLE fields and counts glyph-coloured pixels in the box body for each photograph.

WHAT COUNTS AS DRAWN TEXT. The state must have been the title's conversation state; at least MIN_CAPTURES
captures must have been taken inside it; and the body must hold at least MIN_GLYPH_PIXELS glyph pixels in
its fullest capture, with the first capture (box just opened) holding fewer. A run where the box opens and
no glyph ever appears is refused by name, which is the failure the early capture looked like. The glyph
colours are the two font colours measured from the shipped captures (not asserted by the guest), counted by
exact match so Spyro's orange wings and the HUD cannot be mistaken for text; the body region excludes the
speaker-name strip above the box. Every capture prints its count against the region's pixel total.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from title_profile import Refusal, TitleProfile, profile
from title_route import (
    EXIT_OK,
    EXIT_REFUSED,
    SETTLE,
    STEP,
    Evidence,
    LevelIdentity,
    RoutePort,
    open_port,
    read_level,
    reach_playing,
)

GLYPH_COLOURS = frozenset({(176, 136, 8), (192, 144, 8)})  # the two font colours, measured from the captures
WALK_BUDGET = 400  # fields of walking allowed to open the conversation (Spyro 2 opens it in ~40)
SAMPLE = 30  # fields between captures inside the conversation
CAPTURE_BUDGET = 900  # fields allowed inside the conversation before the route gives up
MIN_CAPTURES = 6
MIN_GLYPH_PIXELS = 1000
# The box body as fractions of the frame, excluding the speaker-name strip that sits above the box.
BODY_X = (0.04, 0.96)
BODY_Y = (0.58, 0.89)


@dataclass(frozen=True)
class Capture:
    path: Path
    glyph_pixels: int
    region_pixels: int


@dataclass
class ConversationEvidence:
    arrival_field: int = 0
    level: LevelIdentity | None = None
    opened_after: int = 0
    captures: list[Capture] = field(default_factory=list)

    def peak(self) -> int:
        return max((c.glyph_pixels for c in self.captures), default=0)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """A binary P6 image: (width, height, RGB bytes). Refuses anything else rather than guessing."""
    data = path.read_bytes()
    fields: list[bytes] = []
    position = 0
    while len(fields) < 4:
        while data[position : position + 1].isspace():
            position += 1
        end = position
        while end < len(data) and not data[end : end + 1].isspace():
            end += 1
        if end == position:
            raise Refusal(f"{path}: truncated PPM header")
        fields.append(data[position:end])
        position = end
    position += 1
    magic, width, height, maxval = fields[0], int(fields[1]), int(fields[2]), int(fields[3])
    if magic != b"P6" or maxval != 255 or len(data) - position != width * height * 3:
        raise Refusal(f"{path}: not an 8-bit binary PPM of the size its header says")
    return width, height, data[position:]


def count_glyph_pixels(width: int, height: int, rgb: bytes) -> tuple[int, int]:
    """(glyph-coloured pixels, pixels examined) over the conversation box body."""
    x0, x1 = int(width * BODY_X[0]), int(width * BODY_X[1])
    y0, y1 = int(height * BODY_Y[0]), int(height * BODY_Y[1])
    glyphs = 0
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(x0, x1):
            offset = row + x * 3
            if (rgb[offset], rgb[offset + 1], rgb[offset + 2]) in GLYPH_COLOURS:
                glyphs += 1
    return glyphs, (x1 - x0) * (y1 - y0)


def capture(port: RoutePort, path: Path) -> Capture:
    port.shot(str(path))
    width, height, rgb = read_ppm(path)
    glyphs, region = count_glyph_pixels(width, height, rgb)
    return Capture(path, glyphs, region)


def _require_dialogue(entry: TitleProfile) -> tuple[int, str]:
    if entry.state_dialogue is None or entry.dialogue_button is None:
        raise Refusal(f"{entry.label}: no conversation state has been observed for this title, so there is no route")
    return entry.state_dialogue, entry.dialogue_button


def converse(port: RoutePort, entry: TitleProfile, shot_dir: Path, root: Path = Path(".")) -> ConversationEvidence:
    """Reach gameplay, walk until the guest's state is the conversation, and photograph the box."""
    state_dialogue, button = _require_dialogue(entry)
    evidence = ConversationEvidence()
    seen = Evidence()
    evidence.arrival_field = reach_playing(port, entry, seen)
    port.run(SETTLE)
    if entry.level is not None:
        evidence.level = read_level(port, entry.level)
    port.press(button)
    walked = 0
    while port.gamestate() != state_dialogue:
        port.run(STEP)
        walked += STEP
        if walked >= WALK_BUDGET:
            port.release(button)
            raise Refusal(f"{entry.label}: {button} for {WALK_BUDGET} fields never opened a conversation (state {state_dialogue})")
    port.release(button)
    evidence.opened_after = walked
    elapsed = 0
    while port.gamestate() == state_dialogue and elapsed < CAPTURE_BUDGET:
        port.run(SAMPLE)
        elapsed += SAMPLE
        evidence.captures.append(capture(port, root / shot_dir / f"talk{len(evidence.captures):02d}.ppm"))
    return evidence


def verdict(entry: TitleProfile, evidence: ConversationEvidence) -> None:
    """Refuse, by name, a conversation whose box never drew text."""
    if len(evidence.captures) < MIN_CAPTURES:
        raise Refusal(f"{entry.label}: only {len(evidence.captures)} captures inside the conversation (< {MIN_CAPTURES})")
    if evidence.peak() < MIN_GLYPH_PIXELS:
        raise Refusal(
            f"{entry.label}: the conversation box opened but its fullest capture holds {evidence.peak()} glyph pixels "
            f"(< {MIN_GLYPH_PIXELS}): the box draws and the text does not"
        )
    if evidence.captures[0].glyph_pixels >= evidence.peak():
        raise Refusal(f"{entry.label}: the first capture already holds the peak, so the text was not revealed over time")


def report(entry: TitleProfile, evidence: ConversationEvidence) -> str:
    lines = [
        f"{entry.label}: conversation opened {evidence.opened_after} fields into the walk "
        f"(arrival at field {evidence.arrival_field})",
    ]
    if evidence.level is not None:
        lines.append(f"  level {evidence.level.name} (id {evidence.level.level_id})")
    lines.append(f"  glyph pixels per capture, every {SAMPLE} fields, of {evidence.captures[0].region_pixels} body pixels:")
    lines += [f"    {c.path.name}: {c.glyph_pixels}" for c in evidence.captures]
    lines.append(f"  peak {evidence.peak()} (>= {MIN_GLYPH_PIXELS} required), first {evidence.captures[0].glyph_pixels}")
    return "\n".join(lines)


class _ScriptedConversation:
    """A port whose conversation draws glyphs `reveal` pixels per capture, for the selftest. `opens` is the
    walk length that opens it; `reveal` 0 is a box that never draws text."""

    def __init__(self, opens: int, reveal: int, root: Path) -> None:
        self.opens, self.reveal, self.root = opens, reveal, root
        self.field, self.walk, self.held, self.talk_shots = 0, 0, False, 0

    def run(self, frames: int) -> int:
        self.field += frames
        if self.held:
            self.walk += frames
        return self.field

    def gamestate(self) -> int:
        if self.field < 100:
            return 0 if self.field < 20 else 11
        if self.field < 300:
            return 5 if self.field >= 250 else 11
        if self.walk >= self.opens and self.talk_shots < 20:
            return 1
        return 0

    def tap(self, button: str, frames: int = 4) -> None:
        return None

    def press(self, button: str) -> None:
        self.held = True

    def release(self, button: str) -> None:
        self.held = False

    def words(self, address: int, count: int = 1) -> list[int]:
        return [0] * count

    def shot(self, path: str) -> None:
        width, height = 512, 240
        pixels = bytearray(width * height * 3)
        if self.walk >= self.opens:
            self.talk_shots += 1
            glyphs = min(self.talk_shots * self.reveal, 400 * 70)
            for index in range(glyphs):
                x, y = int(width * BODY_X[0]) + index % 400, int(height * BODY_Y[0]) + index // 400
                pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3] = bytes((176, 136, 8))
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(f"P6\n{width} {height}\n255\n".encode() + bytes(pixels))


def _selftest() -> int:
    import tempfile

    entry = profile("spyro2")
    entry = TitleProfile(
        entry.label, entry.image, entry.disc_variable, 0x80000000, 11, 5, 0, 0x80000100, "down", 1, "up"
    )
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        def attempt(label: str, scripted: _ScriptedConversation, expect: str | None) -> None:
            nonlocal failures
            try:
                evidence = converse(scripted, entry, Path("talk"), root)
                verdict(entry, evidence)
            except Refusal as refusal:
                ok = expect is not None and expect in str(refusal)
                print(f"  refuses {label}: {refusal}" if ok else f"SELFTEST FAILED: {label}: {refusal}", file=None if ok else sys.stderr)
                failures += 0 if ok else 1
                return
            ok = expect is None and evidence.peak() >= MIN_GLYPH_PIXELS
            print(f"  accepts {label}: peak {evidence.peak()}" if ok else f"SELFTEST FAILED: {label} was accepted", file=None if ok else sys.stderr)
            failures += 0 if ok else 1

        attempt("a conversation whose text is revealed", _ScriptedConversation(40, 150, root), None)
        attempt("a box that never draws a glyph", _ScriptedConversation(40, 0, root), "the box draws and the text does not")
        attempt("a walk that never opens a conversation", _ScriptedConversation(10_000, 150, root), "never opened a conversation")
        attempt("a box already full on its first capture", _ScriptedConversation(40, 5000, root), "not revealed over time")
        try:
            converse(_ScriptedConversation(40, 150, root), profile("spyro3"), Path("talk"), root)
        except Refusal as refusal:
            print(f"  refuses a title with no observed conversation: {refusal}")
        else:
            print("SELFTEST FAILED: spyro3 has no conversation state", file=sys.stderr)
            failures += 1
        try:
            bad = root / "bad.ppm"
            bad.write_bytes(b"P6\n4 4\n255\nxx")
            read_ppm(bad)
        except Refusal as refusal:
            print(f"  refuses a truncated image: {refusal}")
        else:
            print("SELFTEST FAILED: a truncated PPM was read", file=sys.stderr)
            failures += 1
    if failures:
        return 1
    print("title_conversation selftest PASS")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", choices=("spyro2", "spyro3"))
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--log", type=Path, default=Path("scratch/play/title_conversation.log"))
    parser.add_argument("--shot-dir", type=Path, default=Path("scratch/play/talk"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        return _selftest()
    if not args.title:
        parser.error("--title is required")
    import drive

    try:
        entry, port = open_port(args.title, args.executable, args.log)
        _require_dialogue(entry)
        try:
            evidence = converse(port, entry, args.shot_dir, drive.ROOT)
        except drive.Refusal as refusal:
            raise Refusal(str(refusal)) from refusal
        finally:
            port.end()
        print(report(entry, evidence))
        verdict(entry, evidence)
    except Refusal as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        return EXIT_REFUSED
    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
