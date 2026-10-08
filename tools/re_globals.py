#!/usr/bin/env python3
"""re_globals.py — which globals does this guest function touch, and how?

WHY THIS EXISTS. Porting a guest routine needs the ADDRESS of every global it reads or writes, and
the decomp gives names without addresses: its data symbols are placed by the linker, so
`g_GemTotal` appears in a header with no number beside it. Reading those numbers back out of a
disassembly listing by eye is slow and silently wrong whenever the listing desyncs on data.

So this decodes the shipping executable NUMERICALLY. A MIPS global access is a `lui` that loads the
high half into a register followed by an instruction that adds the signed low half, so tracking the
`lui` per register and pairing it with the next use of that register recovers the exact address
without needing the surrounding instruction stream to be valid.

WHAT A NEGATIVE LOOKS LIKE. A function with no global accesses prints its instruction count and
`no lui/lo pairs` — which reads differently from a function that was never decoded, and differently
again from a refusal to open the image. A `lui` whose register is never paired is reported as
`unpaired`, because silently dropping it would hide exactly the access being searched for.
"""

import argparse
import pathlib
import struct
import sys

# The instructions that pair with a lui to form an address, by primary opcode.
LO_OPS = {
    0x09: "addiu", 0x0D: "ori",
    0x20: "lb", 0x21: "lh", 0x23: "lw", 0x24: "lbu", 0x25: "lhu",
    0x28: "sb", 0x29: "sh", 0x2B: "sw",
    0x32: "lwc2", 0x3A: "swc2",
}


class Image:
    """A PS-X EXE mapped at its own load address."""

    def __init__(self, path: pathlib.Path):
        data = path.read_bytes()
        if data[:8] != b"PS-X EXE":
            raise SystemExit(f"re_globals: {path} is not a PS-X EXE (magic {data[:8]!r})")
        _pc, _gp, self.load, self.size = struct.unpack("<IIII", data[0x10:0x20])
        self.text = data[0x800:0x800 + self.size]

    def word(self, address: int) -> int:
        offset = address - self.load
        if offset < 0 or offset + 4 > len(self.text):
            raise SystemExit(
                f"re_globals: 0x{address:08X} is outside the image's "
                f"0x{self.load:08X}-0x{self.load + len(self.text):08X}")
        return struct.unpack_from("<I", self.text, offset)[0]


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def scan(image: Image, start: int, end: int | None):
    """Decode [start, end), or to the function's own `jr $ra` and its delay slot when end is None."""
    hi: dict[int, tuple[int, int]] = {}  # register -> (high half, the pc that set it)
    pairs: list[tuple[int, int, str, int]] = []  # (pc, address, mnemonic, register)
    unpaired: list[tuple[int, int]] = []
    address = start
    instructions = 0
    returning = False
    while end is None or address < end:
        word = image.word(address)
        instructions += 1
        op = word >> 26
        if op == 0x0F:  # lui
            rt = (word >> 16) & 0x1F
            if rt in hi:
                unpaired.append((rt, hi[rt][1]))
            hi[rt] = (word & 0xFFFF, address)
        elif op in LO_OPS:
            rs = (word >> 21) & 0x1F
            if rs in hi:
                high, _ = hi.pop(rs)
                pairs.append(((high << 16) + signed16(word & 0xFFFF), address, LO_OPS[op], rs))
        elif op == 0 and (word & 0x3F) == 0x08 and ((word >> 21) & 0x1F) == 31:
            returning = True  # jr $ra — one delay slot still executes
        address += 4
        if end is None and returning and address > start:
            # consume the delay slot, then stop
            word = image.word(address)
            instructions += 1
            op = word >> 26
            if op in LO_OPS:
                rs = (word >> 21) & 0x1F
                if rs in hi:
                    high, _ = hi.pop(rs)
                    pairs.append(((high << 16) + signed16(word & 0xFFFF), address, LO_OPS[op], rs))
            break
    for register, (_, pc) in hi.items():
        unpaired.append((register, pc))
    unpaired.sort(key=lambda item: item[1])
    return instructions, pairs, unpaired


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("start", help="guest address of the function, e.g. 0x8001973C")
    parser.add_argument("--end", default=None, help="stop here instead of at the function's jr $ra")
    parser.add_argument("--img", default="scratch/assets/spyro1/SCUS_942.28",
                        help="the provisioned executable to decode")
    args = parser.parse_args()

    path = pathlib.Path(args.img)
    if not path.is_file():
        print(f"re_globals: REFUSED — no image at {path}. Provision the title first; this tool "
              f"reads the user's own executable and never ships one.", file=sys.stderr)
        return 2
    image = Image(path)
    start = int(args.start, 0)
    end = int(args.end, 0) if args.end else None
    instructions, pairs, unpaired = scan(image, start, end)

    print(f"0x{start:08X}: decoded {instructions} instruction(s)")
    if not pairs:
        print("  no lui/lo pairs — this function touches no global through a hi/lo pair")
    seen: dict[int, list[str]] = {}
    for address, pc, mnemonic, _ in pairs:
        seen.setdefault(address, []).append(f"{mnemonic}@0x{pc:08X}")
    for address in sorted(seen):
        print(f"  0x{address:08X}  {' '.join(seen[address])}")
    if unpaired:
        print(f"  {len(unpaired)} lui(s) never paired with a low half — an address this tool "
              f"could NOT resolve, not an absence of one:")
        for register, pc in sorted(unpaired, key=lambda item: item[1]):
            print(f"    $r{register} set at 0x{pc:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
