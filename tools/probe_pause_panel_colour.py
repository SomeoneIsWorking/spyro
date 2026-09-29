#!/usr/bin/env python3
"""Recover the PAUSE MENU PANEL's colour byte from the guest image, and prove which definition of the
register that holds it is the one live at the store.

WHY THIS IS A TOOL AND NOT A COMMENT. `docs/issues/0144` recorded the port's `kPanelColourByte =
0xE0` as a defect, and its argument was a *census* -- "scanning all 2,212 words of the handler body for
every write to $s4". A census of register writers is exactly the kind of claim that is wrong in a way
a reader cannot see: an omitted opcode class, a delay-slot branch, or a path that leaves and
re-enters the body all produce a confident answer about a different program. So the answer is computed
here, from bytes, with a control-flow graph, and the graph's incompleteness (an indirect branch) is a
REFUSAL rather than a silently dropped edge.

THE QUESTION, PRECISELY. Handler 0x8001A40C stores the panel's three colour bytes with
`sb $s4, 0x48/0x49/0x4A($s0)` at 0x8001A7D8-0x8001A7E0. The colour is therefore whatever $s4 holds
there. $s4 is written more than once in the body, so the question is not "what does $s4 equal" but
"WHICH of its definitions can reach those three stores". That is a MAY-DEFINITION dataflow over the
body's own control-flow graph, and it is falsifiable: if the two arms were not mutually exclusive the
answer would carry both immediates, which is the case `--selftest` case 2 constructs on purpose.

METHOD, and what each step is allowed to assume:

  * BITS come from the image. `tools/decomp_image.py` owns the file-offset formula and this module
    imports it rather than restating it, because the formula is a claim checked against 62,183
    recorded instructions and a second copy of it would be a second unverified claim about the same
    bytes.
  * DECODE is this module's own opcode table, because Capstone's MIPS detail does not expose a
    register-write set (`regs_access` raises CS_ERR_ARCH on this target -- measured, not assumed).
    Every site the report prints is also shown with Capstone's own mnemonic, so a mis-decode is
    visible in the output rather than only in the conclusion.
  * CONTROL FLOW is intra-handler only. A `jal` is a CALL: $s4 is callee-saved on this ABI, so the
    caller keeps its value, and the successor is the delay slot. An `jalr`/`jr` has no statically
    known target and is reported as a refusal -- an unresolvable edge is a hole in the graph, and a
    hole must not read as "no path".
  * THE INDEPENDENT WITNESS is the vendored decompilation's own `setRGB0(f4, 64, 64, 64)`, which is
    read out of `external/spyro-1/src/gamestates/draw.c` and reported with its line. Its ABSENCE is a
    refusal: a second source that silently stopped agreeing is exactly what would make one source
    load-bearing.

    uv run --frozen python tools/probe_pause_panel_colour.py
    uv run --frozen python tools/probe_pause_panel_colour.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import decomp_image  # noqa: E402  (path prepared above)

# ---- the program, as measured -------------------------------------------------------------------

# The pause menu's draw handler: its own entry, and the address the decompilation's epilogue sits at
# (0x8001A40C + 8840 = 0x8001C69C, and 0x8001C648 is the `lw $ra` the world arm jumps to).
HANDLER_FIRST = 0x8001A40C
HANDLER_LAST = 0x8001C69C

# The three stores that write the panel's colour. Read from the image, not asserted: the tool fails
# if the words here are not `sb $s4, 0x48/0x49/0x4A($s0)`.
PANEL_COLOUR_STORES = (0x8001A7D8, 0x8001A7DC, 0x8001A7E0)
PANEL_COLOUR_OFFSETS = (0x48, 0x49, 0x4A)

# The register the guest holds the colour in, and the three prim-relative offsets its colour bytes
# occupy. The MIPS register NUMBER is 20: $zero is 0 and $s0 is 16, so $s4 is 20. Getting that wrong
# is a one-character bug that would census $s0 instead and find a different, also-plausible answer.
COLOUR_REGISTER = 20  # $s4
COLOUR_BYTE_OFFSETS = (0x48, 0x49, 0x4A)

# The vendored decompilation's own statement of the value, and the file that must state it. The macro
# argument is READ, so a re-decompilation that changed it is reported as a disagreement rather than
# quietly ignored.
#
# IT IS LOCATED BY ITS NEIGHBOURS, not by being the first `setRGB0(f4, ...)`. `f4` is reused for every
# untextured quad in the file and `draw.c` states six of them (112, 64, 96, 40, ...), so "the first
# match" names a DIFFERENT panel and reported a disagreement that was an artifact of the search. The
# anchors are the guest's own statements about the same prim: `f4->code = 0x2A` is the one command
# byte only the panel carries, and the literal 140/372 is the panel's own extent. A witness that has
# to be found by its neighbours is a witness whose absence has to be a refusal, which is what
# `decomp_colour` does when no such block exists.
DECOMP_SOURCE = Path("src/gamestates/draw.c")
DECOMP_PANEL_COLOUR = re.compile(r"setRGB0\(f4,\s*(\d+),\s*(\d+),\s*(\d+)\)")
# The two anchors, as the decompilation writes them. Both are inside the panel's own block.
DECOMP_PANEL_COMMAND = re.compile(r"f4->code\s*=\s*0x2A\b")
# The panel's OWN extent, which is what distinguishes it from the other semi-transparent untextured
# quad in this file. `draw.c` builds two `f4->code = 0x2A` quads: this one at 140..372 and a dialogue
# box at 64..448 (line 1349, colour 96). The command byte alone therefore does NOT identify the panel,
# and an anchor that does not would pick whichever came first. The x extent is the guest's own geometry
# -- the same 140 the port derives from the guest's page branch -- so it is a fact about the panel
# rather than a line number.
DECOMP_PANEL_X0 = re.compile(r"f4->x0\s*=\s*140\b")
DECOMP_PANEL_X1 = re.compile(r"f4->x1\s*=\s*372\b")


class Refusal(Exception):
    """Something this tool needs is absent, unresolved, or disagrees. Raised, never reported as a
    clean result: every one of these cases would otherwise print a number that reads as a finding."""


# ---- decode ------------------------------------------------------------------------------------

# MIPS32 opcodes that do NOT put a general-purpose register in bits 11..16, named by class.
#
# Written out rather than derived from Capstone because Capstone's MIPS detail exposes no
# register-write set (measured: `regs_access()` raises CS_ERR_ARCH on this target). Reading bits
# 11..16 as a GPR destination in any of these classes invents a write, and a census that invents one
# reaches a conclusion about a program nobody ran.
NON_GPR_DEST = (
    frozenset({0x00})                        # SPECIAL -- decided by funct, below
    | frozenset({0x01})                      # REGIMM: bltz/bgez + link forms write $ra (15)
    | frozenset({0x02, 0x03})                # j / jal write PC
    | frozenset({0x04, 0x05, 0x06, 0x07})    # beq bne blez bgtz
    | frozenset({0x0A, 0x0B})                # COP0: mfc0 / mtc0
    | frozenset({0x12, 0x13})                # COP2
    | frozenset({0x11, 0x12, 0x13})          # COP1 branches
    | frozenset({0x14, 0x15, 0x16, 0x17})    # beql bnel blezl bgtzl
    | frozenset({0x28, 0x29, 0x2A})          # sb sh sw
    | frozenset({0x31, 0x32, 0x33, 0x35, 0x36, 0x37, 0x39, 0x3A, 0x3B, 0x3D, 0x3E, 0x3F})  # COP1/2/3
)
# SPECIAL functs that name no rd, by funct: jr/jalr name rs; syscall/break/sync name none; mthi/mtlo
# name none; mult/multu/div/divu accumulate into HI/LO; and 0x30/0x32..0x36/0x38 are traps. sll(0x00),
# mfhi(0x10) and mflo(0x12) are deliberately absent -- they DO write rd.
SPECIAL_NO_RD = frozenset({0x08, 0x09, 0x0C, 0x0D, 0x0F, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B,
                           0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38})
# Opcodes whose GPR DESTINATION is `rt` in bits 20..16 rather than `rd` in bits 15..11: the
# immediate-ALU group, `lui`, the loads, `sw` and `ll`. This is the field that decides whether an
# `addiu` is a definition at all, and getting it wrong makes a census of `addiu` come back EMPTY --
# which is indistinguishable from a body that computes nothing, and is why case 6 pins it.
RT_DEST_OPCODES = frozenset({0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                             0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                             0x2B, 0x30})
# A call: the callee keeps $s4 (callee-saved on this ABI) and control returns to the delay slot.
CALL_FUNCT = 0x09
# Conditional branches, as (opcode, allowed rt for opcode 0x01 REGIMM).
CONDITIONAL_OPCODES = frozenset({0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17})
REGIMM_BRANCH_RT = frozenset({0x00, 0x01, 0x10, 0x11})
COP1_BRANCH_OPCODES = frozenset({0x11, 0x12})
JUMP_OPCODE = 0x02
JAL_OPCODE = 0x03


def opcode(word: int) -> int:
    return (word >> 26) & 0x3F


def destination(word: int) -> int | None:
    """The general-purpose register this instruction writes, or None when it writes no GPR."""
    if opcode(word) == 0x00:
        return None if (word & 0x3F) in SPECIAL_NO_RD else (word >> 11) & 0x1F
    if opcode(word) in NON_GPR_DEST:
        return None
    if opcode(word) in RT_DEST_OPCODES:
        return (word >> 16) & 0x1F
    return (word >> 11) & 0x1F


def branch_target(address: int, word: int) -> int | None:
    """The static branch/jump target, or None for a fallthrough-only or indirect transfer."""
    op = opcode(word)
    if op in CONDITIONAL_OPCODES:
        return address + 4 + (_signed16(word) << 2)
    if op == 0x01 and ((word >> 16) & 0x1F) in REGIMM_BRANCH_RT:
        return address + 4 + (_signed16(word) << 2)
    if op in COP1_BRANCH_OPCODES:
        return address + 4 + (_signed16(word) << 2)
    if op in (JUMP_OPCODE, JAL_OPCODE):
        return ((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
    if op == 0x00 and (word & 0x3F) in (0x08, CALL_FUNCT):
        return None  # jr / jalr: an indirect transfer with no static target
    return None


def is_call(address: int, word: int) -> bool:
    """A call rather than a jump: control comes back to the delay slot, and $s4 survives it."""
    return opcode(word) == JAL_OPCODE or (opcode(word) == 0x00 and (word & 0x3F) == CALL_FUNCT)


def _signed16(word: int) -> int:
    value = word & 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def mnemonic(word: int, address: int) -> str:
    """Capstone's own rendering, for the sites the report prints. Diagnostic text, and the report
    says so -- the conclusion rests on the decode above, not on this string."""
    from capstone import CS_ARCH_MIPS, CS_MODE_LITTLE_ENDIAN, CS_MODE_MIPS32, Cs

    decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    instruction = next(decoder.disasm(word.to_bytes(4, "little"), address, count=1), None)
    if instruction is None or instruction.size != 4:
        return f"UNDECODABLE 0x{word:08X}"
    return f"{instruction.mnemonic} {instruction.op_str}"


# ---- the body, as an address -> word map --------------------------------------------------------


@dataclass(frozen=True)
class Body:
    """One handler body as words. A selftest case builds one of these directly, which is why the
    analysis takes a Body rather than reading the image itself."""

    first: int
    words: dict[int, int]

    def __contains__(self, address: int) -> bool:
        return address in self.words

    def __getitem__(self, address: int) -> int:
        return self.words[address]

    @property
    def last(self) -> int:
        return max(self.words) + 4


def load_body(window: decomp_image.ImageWindow, first: int, last: int) -> Body:
    raw = window.window(first, last)
    return Body(first, {first + offset: int.from_bytes(raw[offset:offset + 4], "little")
                        for offset in range(0, len(raw), 4)})


# ---- control flow ------------------------------------------------------------------------------


def successors(body: Body, address: int) -> list[int]:
    """The intra-handler successors of `address`, including the exits.

    A branch executes its delay slot first, so the fallthrough edge starts at address + 4 and the
    taken edge is the branch's own statically computed target. A call contributes only its
    fallthrough: the callee preserves $s4 on this ABI, and no callee can set a caller-saved value
    the body then reads, so the caller's $s4 is unchanged across it.

    An edge that leaves the body is an EXIT, not a hole, and is returned rather than dropped. A MIPS
    branch or jump target is computed from the instruction word, so an out-of-body target is exactly
    what the guest transfers to -- a `jal` into a producer, or the tail `j` to the epilogue -- and
    dropping it silently would only hide the exit. The one edge this analysis genuinely cannot follow
    is an INDIRECT transfer, and `indirect_transfers` is where that is detected and refused.
    """
    if address not in body:
        return []
    word = body[address]
    if is_call(address, word):
        return [address + 4]
    op = opcode(word)
    unconditional = op in (JUMP_OPCODE, 0x00)  # `j`, and `jr` when branch_target gave a target
    target = branch_target(address, word)
    if target is None:
        return [address + 4]
    if unconditional:
        # An unconditional transfer has ONE successor. Giving it the delay-slot fallthrough as well
        # invents an edge the guest never takes, and here that invention is exactly what would make
        # the world arm's 0xE0 look like it reaches the panel.
        return [target]
    return [address + 4, target]


RETURN_ADDRESS = 31  # $ra


def indirect_transfers(body: Body) -> list[int]:
    """Every `jr`/`jalr`, split by what its target register MEANS.

    A `jr $ra` is a PROCEDURE EXIT and a `jalr $ra` is a call that comes back to its delay slot.
    Neither adds an edge back into the body at a computed point, so neither is a hole in the graph,
    and treating them as one would make every MIPS function with a return look un-analysable.

    A transfer through any OTHER register is a different thing: the guest computed a target this
    analysis cannot follow, and it may well be back inside this body. That IS a hole, it is reported
    as one, and the answer is refused rather than published as an under-count. The distinction is the
    register, not the address -- naming 0x8001C68C here would be a magic offset that keeps working for
    this handler and silently fails on the next one.

    Note the scope this leaves open, stated rather than implied: an indirect transfer could CALL guest
    code that re-enters this handler. That is equally true of every `jal` out of the body, so it is
    the ordinary limit of an intra-procedural analysis and not something this instrument claims.
    """
    exits: list[int] = []
    holes: list[int] = []
    for address, word in sorted(body.words.items()):
        if opcode(word) != 0x00 or (word & 0x3F) not in (0x08, CALL_FUNCT):
            continue
        (exits if ((word >> 21) & 0x1F) == RETURN_ADDRESS else holes).append(address)
    return holes, exits


# ---- the may-definition dataflow ---------------------------------------------------------------


@dataclass(frozen=True)
class Definition:
    address: int
    immediate: int | None
    text: str


def definitions_of(body: Body, register: int) -> list[Definition]:
    out = []
    for address, word in sorted(body.words.items()):
        if destination(word) == register:
            out.append(Definition(address, _signed16(word) if opcode(word) == 0x09 else None,
                                  mnemonic(word, address)))
    return out


def reaching_definitions(body: Body, register: int, uses: tuple[int, ...]) -> dict[int, set[int]]:
    """REACHING definitions at each use: the sites whose write is the value the use actually reads.

    That is a KILL analysis, not a plain union, and the difference is the whole answer. A union-only
    walk would report every definition on any path to a use, so a world arm that falls through a
    later overwrite would still be named as reaching the panel -- reporting one value where two are
    impossible and calling it "a reachability result". So each node's own definition KILLS every
    incoming definition, and the transfer is `(incoming - killed) | {this node}`:

        transfer(incoming) = (incoming \\ gen(node)) | gen(node)

    Termination is immediate and does not rely on monotonicity: the sets only ever shrink toward the
    definitions reachable on the paths that survive, and the worklist drains because a node is
    re-queued only when its set CHANGES, over a finite lattice of subsets of `gen`.

    Every definition scanned is reported separately by `definitions_of`, so a body with two writes
    and one reaching them reads as that, rather than as a body with a single write in it.
    """
    definitions = definitions_of(body, register)
    if not definitions:
        raise Refusal(
            f"no instruction in 0x{body.first:08X}..0x{body.last:08X} writes "
            f"${_register_name(register)}; the uses below would have no definition at all, which "
            f"is a wrong address rather than a missing value")
    gen = {definition.address for definition in definitions}

    def transfer(received: set[int], address: int) -> set[int]:
        """The facts leaving `address`. A definition is a FULL KILL: writing the register makes every
        earlier value unreachable at this point, on every path, so the outgoing set is the singleton --
        not the union that arrived plus this one.

        That distinction is the whole answer, and a union here produces a specific wrong result: on a
        path where a later write follows an earlier one, the earlier write is still in the incoming
        set and would be reported as reaching the use, so a body whose last write is 0x40 would
        report 0xE0 as reaching the panel as well and the two would be indistinguishable from a real
        ambiguity.
        """
        return {address} if address in gen else received

    incoming: dict[int, set[int]] = {body.first: set()}
    outgoing: dict[int, set[int]] = {}
    work = [body.first]
    # A standard monotone worklist. Each `in` only GROWS, over the finite lattice of subsets of
    # `gen`, so it converges. Two details are load-bearing and are the two ways this silently
    # under-counts if they are dropped:
    #   * the entry's `in` is EMPTY, so the test cannot be "did the set change" alone -- the first
    #     propagation would add nothing and the walk would never leave the entry. An unvisited node is
    #     therefore enqueued on FIRST contact whatever it carries.
    #   * a node must be re-enqueued when a LATER edge brings a definition an earlier one did not,
    #     or the first edge to reach it wins and the rest are discarded. `outgoing` is compared, not
    #     mere visitation.
    while work:
        address = work.pop()
        # NO "already produced" early exit. A node's facts grow as later edges deliver them, so a node
        # popped once with a partial set must be processed again when more arrive; skipping it on the
        # second visit discards exactly the definitions that arrive over a second path.
        produced = transfer(incoming.get(address, set()), address)
        if produced == outgoing.get(address):
            continue
        outgoing[address] = produced
        for edge in successors(body, address):
            if edge not in body:
                continue  # an exit: the guest leaves the body here
            # First contact enqueues the edge whatever it carries, INCLUDING an empty set -- a node
            # reached before the first definition is a real path, not an unreached one. And the facts
            # just produced are merged in on that same first contact: seeding it with an empty set and
            # re-deriving later would drop them, and the whole run before the first write is exactly
            # the path that carries the question.
            merged = incoming.get(edge, set()) | produced
            if merged != incoming.get(edge) or edge not in outgoing:
                incoming[edge] = merged
                work.append(edge)
    return {use: transfer(incoming.get(use, set()), use) if use in incoming else set()
            for use in uses}


_REGISTER_NAMES = ("zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
                   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
                   "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra")


def _register_name(register: int) -> str:
    return _REGISTER_NAMES[register] if 0 <= register < len(_REGISTER_NAMES) else f"r{register}"


# The body leaves through `jal` (every producer) and through a tail `j` to its epilogue. Those are
# ordinary exits and `successors` returns them; nothing here is an exception.


def decomp_colour() -> tuple[tuple[int, int, int], int, str, int]:
    """(r, g, b), the line it was read from, the line's text, and how many candidates were rejected.

    The panel's `setRGB0` is the one inside the block that also states `f4->code = 0x2A` and
    `f4->x0 = 140`. Every other `setRGB0(f4, ...)` in the file belongs to a different untextured
    quad, and the count of those is reported rather than dropped: a reader who sees "1 of 6" knows
    the search discriminated, and a tool that silently took the first match would report a confident
    disagreement that is really about the wrong panel.

    Absence of the anchored block is a refusal, because a witness that quietly went missing is how
    one source becomes load-bearing.
    """
    path = TOOLS.parent / "external" / "spyro-1" / DECOMP_SOURCE
    if not path.is_file():
        raise Refusal(f"no vendored decompilation at {path}; the independent witness for the panel "
                      f"colour is not available, so a single source would be the only one")
    lines = path.read_text(errors="replace").splitlines()
    # The panel is the block that states BOTH halves of its own extent, 140 and 372. That pair is what
    # makes it unique: the other semi-transparent untextured quad in this file is 64..448.
    extents = [number for number, line in enumerate(lines, start=1)
               if DECOMP_PANEL_X0.search(line) and DECOMP_PANEL_X1.search(
                   "\n".join(lines[number - 1:number + 1]))]
    if len(extents) != 1:
        raise Refusal(
            f"{path} states the panel's extent (f4->x0 = 140 with f4->x1 = 372) on {len(extents)} "
            f"line(s); the independent witness cannot be resolved to one place, so a single source "
            f"would be the only one")
    extent_line = extents[0]
    candidates = [number for number, line in enumerate(lines, start=1)
                  if DECOMP_PANEL_COLOUR.search(line)]
    for number in candidates:
        # Same block, bounded distance: a quad that appears 30 lines away is a different prim even
        # though the file reuses the `f4` name throughout.
        if abs(number - extent_line) <= 24:
            match = DECOMP_PANEL_COLOUR.search(lines[number - 1])
            triple = tuple(int(match.group(index)) for index in (1, 2, 3))
            return triple, number, lines[number - 1].strip(), len(candidates) - 1  # type: ignore[return-value]
    raise Refusal(
        f"{path} states {len(candidates)} setRGB0(f4, ...) but none of them is within the panel's own "
        f"block (its extent is at line {extent_line}); the independent witness for the panel colour "
        f"is not available, so a single source would be the only one")


# ---- the verdict -------------------------------------------------------------------------------


def investigate(body: Body, uses: tuple[int, ...], register: int = COLOUR_REGISTER) -> dict:
    definitions = definitions_of(body, register)
    reaching = reaching_definitions(body, register, uses)
    # The stores must be the shape the question assumes. Without this the dataflow would answer a
    # question about whatever else happens to read the register. `sb` encodes base in bits 25..21
    # and the SOURCE register in bits 20..16, so the check is on bits 20..16 and the immediate.
    for use, offset in zip(uses, COLOUR_BYTE_OFFSETS):
        word = body[use]
        if (opcode(word) != 0x28 or ((word >> 16) & 0x1F) != register
                or (word & 0xFFFF) != offset):
            raise Refusal(
                f"0x{use:08X} is not `sb ${_register_name(register)}, 0x{offset:02X}($base)` as the "
                f"panel's colour store was assumed to be: the image says "
                f"`{mnemonic(word, use)}`. Refusing rather than analysing a different instruction.")
    holes, exits = indirect_transfers(body)
    if holes:
        raise Refusal(
            f"{len(holes)} indirect transfer(s) through a non-return register in "
            f"0x{body.first:08X}..0x{body.last:08X} (first 0x{holes[0]:08X}); the "
            f"reaching-definition answer below would be an under-count, which is indistinguishable "
            f"from the answer with no indirect path.")
    return {"definitions": definitions, "reaching": reaching, "exits": exits}


def report(window: decomp_image.ImageWindow) -> int:
    body = load_body(window, HANDLER_FIRST, HANDLER_LAST)
    words = len(body.words)
    verdict = investigate(body, PANEL_COLOUR_STORES)
    by_address = {definition.address: definition for definition in verdict["definitions"]}
    print(f"[panel-colour] handler 0x{HANDLER_FIRST:08X}..0x{HANDLER_LAST:08X}: scanned {words} "
          f"word(s); {words * 4} bytes")
    print(f"[panel-colour] indirect transfer(s) through $ra (procedure exits, followed by no edge "
          f"back into the body): {len(verdict['exits'])}")
    print(f"[panel-colour] writes to ${_register_name(COLOUR_REGISTER)}: "
          f"{len(verdict['definitions'])}")
    for definition in verdict["definitions"]:
        print(f"[panel-colour]   def 0x{definition.address:08X}  0x{body[definition.address]:08X}  "
              f"{definition.text}")
    for use, sites in sorted(verdict["reaching"].items()):
        names = ", ".join(f"0x{site:08X}" for site in sorted(sites)) or "(none)"
        print(f"[panel-colour] use 0x{use:08X}  {mnemonic(body[use], use)}  <- {names}")
    reachable = set().union(*verdict["reaching"].values()) if verdict["reaching"] else set()
    if len(reachable) != 1:
        print(f"[panel-colour] REFUSED: {len(reachable)} definitions reach the panel's colour "
              f"stores, so there is no single value to report. Scanned "
              f"{len(verdict['definitions'])}; reachable {sorted(f'0x{s:08X}' for s in reachable)}.")
        return 1
    site = next(iter(reachable))
    immediate = by_address[site].immediate
    if immediate is None:
        print(f"[panel-colour] REFUSED: the reachable definition at 0x{site:08X} is not an "
              f"`addiu ${_register_name(COLOUR_REGISTER)}, $zero, imm`, so the value is computed "
              f"rather than materialised and this probe does not model that.")
        return 1
    value = immediate & 0xFF
    triple, line, text, rejected = decomp_colour()
    print(f"[panel-colour] PANEL COLOUR BYTE = 0x{value:02X} = {value}  "
          f"(definition 0x{site:08X}, the only one of {len(verdict['definitions'])} that reaches "
          f"the three stores)")
    print(f"[panel-colour] independent witness {DECOMP_SOURCE}:{line}: {text} -> "
          f"rgb({triple[0]}, {triple[1]}, {triple[2]})")
    print(f"[panel-colour] witness search: 1 accepted, {rejected} other setRGB0(f4, ...) in the file "
          f"rejected as a different prim's")
    agree = value == triple[0] == triple[1] == triple[2]
    print(f"[panel-colour] image and decompilation {'AGREE' if agree else 'DISAGREE'}")
    return 0 if agree else 1


# ---- the negative set --------------------------------------------------------------------------
#
# Hermetic: it builds BODIES, never reads the image, so it cannot pass by having a corpus. Case 2 is
# the one that matters -- it is the shape this argument would take if the two arms were not mutually
# exclusive, and a weaker analysis would still report one value there.


def _body(words: dict[int, int]) -> Body:
    return Body(min(words), dict(words))


STORE_OPCODE = 0x28  # sb
PRIM_CURSOR_REGISTER = 16  # $s0, the register the guest holds the panel prim address in


def _addiu_s4(immediate: int) -> int:
    """`addiu $s4, $zero, imm` -- the shape the one live definition has."""
    return (0x09 << 26) | (0 << 21) | (COLOUR_REGISTER << 16) | (immediate & 0xFFFF)


def _store_s4(offset: int) -> int:
    """`sb $s4, offset($s0)` -- the panel's colour store."""
    return (STORE_OPCODE << 26) | (PRIM_CURSOR_REGISTER << 21) | (COLOUR_REGISTER << 16) \
        | (offset & 0xFFFF)


def _bnez_v0(address: int, target: int) -> int:
    """`bnez $v0, target` -- the guest's own arm selector at 0x8001A43C."""
    return (0x05 << 26) | (2 << 21) | (((target - address - 4) >> 2) & 0xFFFF)


# Named encoders for the classes `destination` has to tell apart. A hex literal here would hide the
# very thing the case is about -- WHICH FIELD carries the register -- so each one names its shape.
def _lui(rt: int) -> int:
    """`lui rt, imm` -- I-type, destination in bits 20..16."""
    return (0x0F << 26) | ((rt & 0x1F) << 16)


def _sll(rd: int) -> int:
    """`sll rd, rt, 0` -- R-type SPECIAL, destination in bits 15..11."""
    return (rd & 0x1F) << 11


def _sllv(rd: int) -> int:
    """`sllv rd, rt, rs` -- R-type, the same rd field as sll."""
    return ((rd & 0x1F) << 11) | (4 << 6)


def _beql() -> int:
    """`beql $zero, $zero, 0` -- a branch-likely that names no register."""
    return (0x14 << 26)


def _jr(rs: int = RETURN_ADDRESS) -> int:
    """`jr rs` -- SPECIAL funct 8, which names rs and no rd."""
    return (8 << 0) | ((rs & 0x1F) << 21)


def _bc1f() -> int:
    """`bc1f 0` -- a COP1 branch."""
    return (0x11 << 26)


def _mult() -> int:
    """`mult $zero, $zero` -- SPECIAL funct 0x18, which accumulates into HI/LO."""
    return (0x18 << 0)


def _multu(rt: int) -> int:
    """`multu $zero, rt` -- SPECIAL funct 0x19, whose rt is an OPERAND, not a destination."""
    return (0x19 << 0) | ((rt & 0x1F) << 16)


def _mthi(rs: int) -> int:
    """`mthi rs` -- SPECIAL funct 0x11, which names no rd. (funct 0x0A/0x0B are movz/movn, which do.)"""
    return (0x11 << 0) | ((rs & 0x1F) << 21)


def _mfhi(rd: int) -> int:
    """`mfhi rd` -- SPECIAL funct 0x10, which DOES write rd."""
    return (0x10 << 0) | ((rd & 0x1F) << 11)


def _jump(target: int) -> int:
    return (JUMP_OPCODE << 26) | ((target >> 2) & 0x03FFFFFF)


# A COMPACT body with the real handler's shape: one branch selecting between two arms, the world arm
# writing the colour register and LEAVING, and the menu arm writing it again and reaching the panel's
# three colour stores. The addresses are synthetic because the cases below are about the SHAPE, and
# the real body is what `report` measures; a case built on the real addresses would pass or fail for
# reasons that have nothing to do with the property under test.
BASE = 0x8001A000
BRANCH = BASE + 0x00
WORLD_DEF = BASE + 0x08
WORLD_EXIT = BASE + 0x0C
MENU_DEF = BASE + 0x10
LOOP_BACK = BASE + 0x14
SELFTEST_STORES = (BASE + 0x18, BASE + 0x1C, BASE + 0x20)
NOP = 0x00000000


def _two_arm_body() -> Body:
    return _body({
        BRANCH: _bnez_v0(BRANCH, MENU_DEF),
        BRANCH + 4: NOP,                                     # the branch's delay slot
        WORLD_DEF: _addiu_s4(0x00E0),                        # the world arm's colour
        WORLD_EXIT: _jump(0x8001C648),                       # and it leaves for the epilogue
        MENU_DEF: _addiu_s4(0x0040),                         # the menu arm's colour
        LOOP_BACK: _bnez_v0(LOOP_BACK, MENU_DEF),            # the four-tile loop's back edge
        SELFTEST_STORES[0]: _store_s4(0x48),
        SELFTEST_STORES[1]: _store_s4(0x49),
        SELFTEST_STORES[2]: _store_s4(0x4A),
    })


def selftest() -> int:
    failures: list[str] = []

    def expect(condition: bool, name: str) -> None:
        if not condition:
            failures.append(name)

    # Case 1 -- THE SHAPE THIS ARGUMENT DEPENDS ON. A `j` leaves the arm that wrote 0xE0, so only
    # the 0x40 definition reaches the three colour stores. Exactly one of two, and BOTH writes are
    # reported: a census that named only the reachable one could not be told from a body with a
    # single write in it, which is the shape a "found nothing else" reading mistakes for absence.
    body = _two_arm_body()
    verdict = investigate(body, SELFTEST_STORES)
    reachable = set().union(*verdict["reaching"].values())
    expect(reachable == {MENU_DEF},
           f"case 1: only the menu arm's definition reaches the stores (got "
           f"{sorted(f'0x{s:08X}' for s in reachable)})")
    expect(len(verdict["definitions"]) == 2,
           f"case 1: both definitions are reported, not just the reachable one "
           f"(got {len(verdict['definitions'])})")
    for use, sites in sorted(verdict["reaching"].items()):
        expect(sites == {MENU_DEF},
               f"case 1: every one of the three stores sees the same definition "
               f"(0x{use:08X} saw {sorted(f'0x{s:08X}' for s in sites)})")

    # Case 2 -- THE DISCRIMINATOR, and it is a DIFFERENT leak from the one it replaces. Making the
    # world arm fall through is not enough: it then runs THROUGH the menu arm's own 0x40 write, so
    # the stores still see exactly one definition and a probe reporting one value here would be
    # RIGHT. The leak that matters is a world arm that reaches the panel WITHOUT passing the
    # definition that overwrites it -- which is the shape this analysis exists to catch, and the one
    # a reachability answer that stops at the first write would report as a single clean value.
    leaky = _body({BRANCH: _bnez_v0(BRANCH, MENU_DEF),
                   BRANCH + 4: NOP,
                   WORLD_DEF: _addiu_s4(0x00E0),
                   WORLD_DEF + 4: NOP,
                   WORLD_DEF + 8: NOP,
                   WORLD_DEF + 12: NOP,
                   SELFTEST_STORES[0]: _store_s4(0x48),
                   SELFTEST_STORES[1]: _store_s4(0x49),
                   SELFTEST_STORES[2]: _store_s4(0x4A)})
    leaky_reaching = set().union(*investigate(leaky, SELFTEST_STORES)["reaching"].values())
    expect(leaky_reaching == {WORLD_DEF},
           f"case 2: a world arm reaching the panel WITHOUT passing the overwriting definition is "
           f"reported (got {sorted(f'0x{s:08X}' for s in leaky_reaching)})")
    # ... and the benign version of the same edit must still report the overwriting definition, so
    # case 2a is not satisfied by an analysis that simply always returns every definition it found.
    through = _body({**body.words, WORLD_EXIT: NOP})
    through_reaching = set().union(*investigate(through, SELFTEST_STORES)["reaching"].values())
    expect(through_reaching == {MENU_DEF},
           f"case 2a: a world arm that falls through the overwrite is NOT reported as reaching the "
           f"panel (got {sorted(f'0x{s:08X}' for s in through_reaching)})")

    # Case 3 -- A PERTURBED IMMEDIATE IS READ, NOT ASSUMED. Replacing the menu arm's 0x40 with
    # 0xE0 moves the answer's immediate. A probe that still reported 0x40 here is returning a
    # constant, which is exactly the defect this instrument exists to make impossible.
    perturbed = _body({**body.words, MENU_DEF: _addiu_s4(0x00E0)})
    perturbed_reaching = set().union(*investigate(perturbed, SELFTEST_STORES)["reaching"].values())
    expect(perturbed_reaching == {MENU_DEF},
           "case 3: the perturbed body still has one reachable definition")
    if perturbed_reaching:
        site = next(iter(perturbed_reaching))
        expect(perturbed[site] & 0xFFFF == 0x00E0,
               "case 3: the perturbed immediate is what the answer would report")
    else:
        failures.append("case 3: the perturbed body reported no reachable definition at all")

    # Case 4 -- AN INDIRECT TRANSFER THROUGH A COMPUTED REGISTER IS A REFUSAL, not a dropped edge.
    # `jr $s0` may re-enter the body at a point this analysis cannot see, so an answer that ignored
    # it would be an under-count dressed as a measurement.
    indirect = _body({**body.words, WORLD_EXIT: _jr(PRIM_CURSOR_REGISTER)})
    try:
        investigate(indirect, SELFTEST_STORES)
        failures.append("case 4: an indirect transfer must REFUSE, not analyse around it")
    except Refusal as error:
        expect("indirect" in str(error), f"case 4: the refusal names the indirect transfer ({error})")
    # Case 4a -- AND `jr $ra` IS NOT. The real handler ends in one, and treating a procedure exit as a
    # graph hole would make every function with a return un-analysable -- a refusal that is always
    # right is not a discriminator.
    returning = investigate(_body({**body.words, WORLD_EXIT: _jr(RETURN_ADDRESS)}), SELFTEST_STORES)
    expect(len(returning["exits"]) == 1,
           f"case 4a: `jr $ra` is reported as an exit, not as a hole (exits "
           f"{len(returning['exits'])})")
    expect(set().union(*returning["reaching"].values()) == {MENU_DEF},
           "case 4a: the answer is unchanged by the body's own return")

    # Case 5 -- A USE THAT IS NOT THE ASSUMED INSTRUCTION is a refusal, not a different question.
    # Feeding the panel's stores a `sw` would make the dataflow answer about a word store.
    wrong_use = _body({**body.words,
                       SELFTEST_STORES[0]: (0x2A << 26) | (PRIM_CURSOR_REGISTER << 21)
                       | (COLOUR_REGISTER << 16) | 0x148})  # sw $s4, 0x148($s0)
    try:
        investigate(wrong_use, SELFTEST_STORES)
        failures.append("case 5: a use that is not the panel's colour store must REFUSE")
    except Refusal as error:
        expect("not `sb" in str(error), f"case 5: the refusal names the instruction shape ({error})")

    # Case 6 -- THE DECODE ITSELF, on the field encodings that are easiest to get wrong. These are
    # built from named ENCODERS rather than written as hex, so a reader can see which field each
    # assertion is about: an I-type destination is bits 20..16, an R-type one is bits 15..11, and the
    # encodings that LOOK like they write rd but do not are exactly the ones that would silently
    # invent a definition and make the answer about a different program.
    expect(destination(_addiu_s4(0x40)) == COLOUR_REGISTER,
           "case 6: addiu names $s4 as rt (bits 20..16, NOT 15..11)")
    expect(destination(_lui(7)) == 7, "case 6: lui names its rt")
    expect(destination(_sll(COLOUR_REGISTER)) == COLOUR_REGISTER, "case 6: sll names its rd")
    expect(destination(_sllv(COLOUR_REGISTER)) == COLOUR_REGISTER, "case 6: sllv names its rd")
    expect(destination(_store_s4(0x48)) is None, "case 6: sb names no rd")
    expect(destination(_beql()) is None, "case 6: beql names no rd")
    expect(destination(_jr()) is None, "case 6: jr names no rd")
    expect(destination(_bc1f()) is None, "case 6: a COP1 branch names no GPR rd")
    expect(destination(_mult()) is None, "case 6: mult accumulates to HI/LO and names no rd")
    expect(destination(_multu(COLOUR_REGISTER)) is None,
           "case 6: multu puts its operand in bits 20..16, which is not a destination")
    expect(destination(_mthi(COLOUR_REGISTER)) is None, "case 6: mthi names no rd")
    expect(destination(_mfhi(COLOUR_REGISTER)) == COLOUR_REGISTER, "case 6: mfhi DOES name its rd")

    # Case 7 -- AN EMPTY BODY IS A REFUSAL, not "the colour is whatever it was".
    try:
        investigate(_body({SELFTEST_STORES[0]: _store_s4(0x48)}), SELFTEST_STORES)
        failures.append("case 7: a body with no $s4 definition must REFUSE")
    except Refusal as error:
        expect("no instruction" in str(error), f"case 7: the refusal names the absent definition ({error})")

    # Case 8 -- A CYCLE. The four tiled quads are a loop, so the real body has a back edge, and a
    # worklist that mishandles one either fails to converge or loses the definition on the edge.
    loop_only = _body({BRANCH: _jump(MENU_DEF),
                       MENU_DEF: _addiu_s4(0x0040),
                       LOOP_BACK: _bnez_v0(LOOP_BACK, MENU_DEF),
                       SELFTEST_STORES[0]: _store_s4(0x48),
                       SELFTEST_STORES[1]: _store_s4(0x49),
                       SELFTEST_STORES[2]: _store_s4(0x4A)})
    loop_reaching = set().union(*investigate(loop_only, SELFTEST_STORES)["reaching"].values())
    expect(loop_reaching == {MENU_DEF},
           f"case 8: a self-looping arm converges on one definition per store (got "
           f"{sorted(f'0x{s:08X}' for s in loop_reaching)})")

    for name in failures:
        print(f"FAIL: {name}")
    if failures:
        print(f"probe_pause_panel_colour selftest: {len(failures)} failure(s)")
        return 1
    print("probe_pause_panel_colour selftest: PASS (9 cases, 6 negative)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true",
                        help="run the negative set on synthetic bodies; no image, no product")
    parser.add_argument("--image", default=None,
                        help="an alternative provisioned executable (default: the admitted image)")
    arguments = parser.parse_args()
    if arguments.selftest:
        return selftest()
    try:
        window = decomp_image.ImageWindow.open(
            Path(arguments.image) if arguments.image else None)
        return report(window)
    except decomp_image.ImageRefusal as error:
        print(f"[panel-colour] REFUSED: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
