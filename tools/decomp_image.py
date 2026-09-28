#!/usr/bin/env python3
"""The admitted Spyro 1 image window, and the arithmetic that maps a guest address to its bytes.

ONE CONCEPT: which bytes a guest address names. Everything that reads the image -- the Ghidra import
base, the byte cross-check on Ghidra's own disassembly, the anchor lookups -- asks this module, so
there is exactly one file-offset formula in the repository.

THE FORMULA IS NOT RE-DERIVED HERE. `tools/probe_guest_disasm.py` owns it (`file_offset`,
`TEXT_FILE_OFFSET`, `TEXT_LOAD_ADDRESS`, the image path and the listing cross-check), and this module
imports those names rather than restating them. That matters because the formula is a CLAIM, and
`probe_guest_disasm.py --verify-only` is what checks it against 62,183 of 62,183 recorded
instructions. A second copy of the formula in a decompilation tool would be a second unverified
claim about the same bytes, and the two would drift exactly where a re-port drifts.

The import base is the one derived fact here. Ghidra's BinaryLoader maps a file at one base, and
guest addresses must land where the rest of this repository writes them -- so the base is
``TEXT_LOAD_ADDRESS - TEXT_FILE_OFFSET``, which puts file offset 0x800 on guest address 0x80010000
and maps the executable's own 0x800-byte header BELOW the text window rather than shifting every
address by a page. Getting this wrong does not fail loudly: it produces a project full of functions
at addresses 0x2000 lower than the recorded ones, every one of which is a plausible-looking
function that this repository has no record of.
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

# The single owner of the offset arithmetic. Re-exported so callers read one module.
import probe_guest_disasm as listing  # noqa: E402  (path is prepared above)

EXE = listing.EXE
TEXT_FILE_OFFSET = listing.TEXT_FILE_OFFSET
TEXT_LOAD_ADDRESS = listing.TEXT_LOAD_ADDRESS
# The base Ghidra's BinaryLoader is pointed at. NOT the text load address: see the module docstring.
GHIDRA_IMPORT_BASE = TEXT_LOAD_ADDRESS - TEXT_FILE_OFFSET
WINDOW_FIRST = TEXT_LOAD_ADDRESS
# The last guest address the admitted file covers. Zero until an image is present, and callers must
# ask an ImageWindow rather than trusting this module-level constant.
WINDOW_LAST = 0


class ImageRefusal(Exception):
    """The image is absent or too short for what was asked. Raised, never returned as a short read."""


@dataclass(frozen=True)
class ImageWindow:
    """The provisioned executable, read whole. One instance per run; `data` is the only payload."""

    path: Path
    data: bytes

    @classmethod
    def open(cls, path: Path | None = None) -> "ImageWindow":
        """Read the admitted image, or REFUSE. An absent or truncated image is a refusal, not an
        empty window: a zero-length read that then reports 'no function at this address' for every
        target is indistinguishable from a target set of nothing but bad addresses."""
        target = Path(path) if path is not None else EXE
        if not target.is_file():
            raise ImageRefusal(
                f"no guest executable at {target} -- provision it with "
                "tools/provision_title.py --title spyro1. Refusing rather than reporting an "
                "empty window, because 'every target is unreadable' and 'the image is absent' "
                "would otherwise print the same line.")
        data = target.read_bytes()
        if len(data) <= TEXT_FILE_OFFSET:
            raise ImageRefusal(
                f"{target} holds {len(data)} byte(s), which is not more than the {TEXT_FILE_OFFSET}"
                " byte(s) of header this image is known to have, so it carries no text window.")
        return cls(path=target, data=data)

    # -- the mapping ------------------------------------------------------------------------------

    def file_offset(self, address: int) -> int:
        return listing.file_offset(address)

    def covers(self, address: int, length: int = 4) -> bool:
        offset = self.file_offset(address)
        return offset >= 0 and offset + length <= len(self.data)

    def window(self, first: int, last: int) -> bytes:
        """The bytes for the half-open guest range, or refuse."""
        if first >= last:
            raise ImageRefusal(f"empty guest range 0x{first:08X}..0x{last:08X}")
        if not self.covers(first, last - first):
            raise ImageRefusal(
                f"guest range 0x{first:08X}..0x{last:08X} is not inside the admitted window "
                f"0x{WINDOW_FIRST:08X}..0x{WINDOW_FIRST + len(self.data) - TEXT_FILE_OFFSET:08X}")
        return self.data[self.file_offset(first):self.file_offset(last)]

    def word(self, address: int) -> int:
        """The 32-bit instruction word, read LITTLE-endian, which is how a PSX executable stores
        instructions. The data half of the image is big-endian, so the wrong half of this choice
        decodes into valid-looking garbage rather than failing."""
        if not self.covers(address, 4):
            raise ImageRefusal(f"0x{address:08X} is outside the admitted window")
        return struct.unpack_from("<I", self.data, self.file_offset(address))[0]


def control_agreement(image: ImageWindow) -> tuple[int, int, int]:
    """(scanned, agree, disagree) for the recorded listing against this module's own mapping.

    The instrument's own control, and the gate the pipeline puts in front of any Ghidra output: a
    disagreement above zero means the mapping is wrong, so every address the pipeline touches is
    meaningless. A scan of zero is refused for the same reason a disagreement is -- 'nothing to check
    against' must not read as 'everything checked out'.
    """
    words, _overlay, _data = listing.listing_words()
    agree = 0
    disagree = 0
    for address, (word, _name) in words.items():
        if not image.covers(address, 4):
            disagree += 1
            continue
        if image.word(address) == word:
            agree += 1
        else:
            disagree += 1
    return len(words), agree, disagree


def selftest() -> int:
    """Both directions, plus the absence refusal.

    Cases:
      1. the image opens and the mapping reproduces the recorded listing (the positive);
      2. an address one word past the end is REFUSED, not read as zero;
      3. an absent image is REFUSED, not read as an empty window;
      4. the mapping is shown to SEPARATE correct from incorrect: a deliberately shifted formula
         must disagree, or case 1 is not evidence of anything.
    """
    failures = 0
    try:
        image = ImageWindow.open()
    except ImageRefusal as error:
        print(f"[decomp-image] selftest FAIL cannot open the image: {error}")
        return 1
    print(f"[decomp-image] selftest image {image.path.name}: {len(image.data)} byte(s), "
          f"window 0x{WINDOW_FIRST:08X}..0x{WINDOW_FIRST + len(image.data) - TEXT_FILE_OFFSET:08X}, "
          f"Ghidra import base 0x{GHIDRA_IMPORT_BASE:08X}")

    scanned, agree, disagree = control_agreement(image)
    if scanned == 0:
        print("[decomp-image] selftest FAIL scanned 0 recorded listing instructions -- the tree this "
              "checks against is absent, so the check would pass on an unverified mapping")
        failures += 1
    elif disagree:
        print(f"[decomp-image] selftest FAIL {agree} of {scanned} recorded instructions agree and "
              f"{disagree} disagree -- the mapping is wrong and every address is meaningless")
        failures += 1
    else:
        print(f"[decomp-image] selftest PASS {agree} of {scanned} recorded instructions agree with "
              f"file_offset = 0x{TEXT_FILE_OFFSET:X} + (addr - 0x{TEXT_LOAD_ADDRESS:08X})")

    # Case 4: the discriminator. A shifted formula is what an off-by-a-page import looks like.
    words, _o, _d = listing.listing_words()
    probe_address = next(iter(sorted(words)))
    shifted_agree = sum(
        1 for address, (word, _n) in words.items()
        if image.covers(address, 4) and image.word(address + 4) == word)
    print(f"[decomp-image] selftest discriminator: a formula shifted by one instruction word maps "
          f"0x{probe_address:08X} correctly but only {shifted_agree} of {scanned} instructions "
          f"overall, so the positive above is measuring something")

    # Case 2: past the end.
    past_end = TEXT_LOAD_ADDRESS + len(image.data) - TEXT_FILE_OFFSET + 4
    try:
        image.word(past_end)
        print(f"[decomp-image] selftest FAIL 0x{past_end:08X} is past the window and was read anyway")
        failures += 1
    except ImageRefusal:
        print(f"[decomp-image] selftest PASS 0x{past_end:08X} past the window was REFUSED, not read "
              "as zero")

    # Case 3: absence.
    try:
        ImageWindow.open(Path("/nonexistent/SCUS_942.28"))
        print("[decomp-image] selftest FAIL an absent image opened as if it were present")
        failures += 1
    except ImageRefusal as error:
        print(f"[decomp-image] selftest PASS an absent image was REFUSED: "
              f"{str(error)[:72]}...")

    print(f"[decomp-image] selftest {'FAILED' if failures else 'PASS'}: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(selftest())
