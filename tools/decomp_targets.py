#!/usr/bin/env python3
"""The named decompilation targets, why each one is here, and what must be true of its output.

ONE CONCEPT: the work order. The operator's directive is "decompile as much guest code as NEEDED so
the bugs we see can be debugged", and `psxport/docs/issues/0135` fixes the filter: recover guest code
that (a) sits on the path of a defect the player can SEE and (b) has no native owner yet. This module
IS that filter, as data. An address that is not named here is not decompiled, so the corpus stays
bounded and every decompile answers a question somebody asked.

ROLES, and they are not interchangeable:
  * `control` -- a function whose bytes are already recorded in this repository, used to prove the
    PIPELINE is trustworthy before any new output is believed. A control that fails stops the work.
  * `chain`   -- a function on the path of the visible defect, recovered to name a layout or a
    branch. A chain target that fails is a finding, not a reason to believe the rest.

ANCHORS are the demanded evidence inside a body, as hex VALUES. They are read out of the IMAGE, not
out of a previous tool's output, and each one is a value the listing or the disassembly probe shows
inside that function -- so a body that stops short of it is a TRUNCATION and must be refused rather
than filed (see decomp_manifest rule 6). Matching is on the hex digits, because the textual FORM of
a constant is a decompiler's choice and the value is a fact.
"""

from __future__ import annotations

from dataclasses import dataclass


class TargetRefusal(Exception):
    """A target set that cannot be resolved. Never returns an empty set, which would read as
    'nothing to decompile' rather than 'the request was wrong'."""


@dataclass(frozen=True)
class Target:
    address: int
    name: str
    role: str            # "control" | "chain"
    anchors: tuple       # hex values that must appear in the decompiled body
    reason: str

    def __str__(self) -> str:
        return f"0x{self.address:08X} {self.name} [{self.role}]"


# ---------------------------------------------------------------------------------------------
# THE CONTROL. Chosen because this repository already has its ground truth, so agreement is a
# checkable fact rather than a judgement.
#
# 0x800258F0 is `func_800258F0`, the S_World renderer RenderWorldChunks, the function the producer
# census (scratch/logs/drive.log, 2026-09-28) names as `world:static` -- 2,776,584 native prims over
# 2,281 frames, the second largest producer in the run.
#
# Its four anchors, each read out of the image:
#   0x80078560  g_EnvironmentAnimations. `docs/issues/0134` records the `lui`/`addiu` pair that forms
#               it at 0x80025BC8/0x80025BCC, once per animation channel, so it recurs through the
#               body rather than sitting at its head.
#   0x1100984A  the GTE `INTPL` command word, at 0x80025CC4 and 0x80025F80 per 0134.
#   0x1000784A  the GTE `DPCS` command word, at 0x80025E38/0x80026134/0x8002617C per 0134.
#   0xE100      the semi-transparent water colour the LQ face path writes, at 0x8002654C and
#               0x80026700. Chosen as the DEEPEST anchor on purpose: it sits at 0x8002654C, roughly
#               a third of the way into a 6,577-line listing, so any truncation before it is caught.
#               It is also the finding itself, so its presence is what makes the decompile evidence
#               for the water branch rather than merely evidence that the pipeline ran.
CONTROL = Target(
    address=0x800258F0,
    name="func_800258F0 (RenderWorldChunks, the S_World renderer)",
    role="control",
    anchors=(0x80078560, 0x1100984A, 0x1000784A, 0xE100),
    reason="known-good control: the four animation channels and the LQ water colour are already "
           "recorded out of the image by docs/issues/0134 and by tools/probe_guest_disasm.py, so a "
           "decompile of it that disagrees is a pipeline fault rather than a discovery.",
)

# ---------------------------------------------------------------------------------------------
# THE CHAIN. The visible defect is the pool water rendering as per-block colour noise
# (scratch/screenshots/field-16x9-interp.png, 684x240) while actors, hedges, towers and buildings
# are clean. Per-block COLOUR with everything else clean is a per-block DATA fault, and the data is
# guest-authored, so the layout has to be named before the read can be judged.
#
# The census already answered "which producer": 13 native producers, no row named for water, and
# the world renderer is the one that draws the terrain the pool is cut into. The two questions left
# are (1) what a world face's per-block colour actually is, and (2) whether the byte at material
# bit 2 really is the semi-transparent/water decision. Both are answered inside 0x800258F0, so the
# chain is one function plus the environment setup that authors the chunk arrays.
CHAIN = (
    Target(
        address=0x8002B9CC,
        name="func_8002B9CC (create the environment: authors the chunk and material arrays)",
        role="chain",
        anchors=(),
        reason="the guest function that AUTHORS the per-chunk arrays the water face reads; its "
               "layout is the other half of the black box, and it is where a stride error would "
               "be introduced rather than merely misread.",
    ),
)


def by_role(role: str) -> tuple:
    if role not in ("control", "chain", "all"):
        raise TargetRefusal(
            f"unknown target role '{role}'. Use 'control', 'chain' or 'all'. Returning an empty set "
            "here would read as 'there is nothing to decompile' rather than 'the request was "
            "wrong'.")
    every = (CONTROL,) + CHAIN
    if role == "all":
        return every
    return tuple(target for target in every if target.role == role)


def resolve(name: str) -> Target:
    """One named target, or a refusal. `name` is a role or a hex address."""
    text = name.strip()
    if text.lower() in ("control", "chain", "all"):
        found = by_role(text.lower())
        if len(found) == 1:
            return found[0]
        return found[0] if found else None
    try:
        address = int(text, 16)
    except ValueError as error:
        raise TargetRefusal(
            f"'{name}' is neither a role (control, chain, all) nor a hex guest address. Refusing "
            "rather than returning an empty target set.") from error
    for target in (CONTROL,) + CHAIN:
        if target.address == address:
            return target
    raise TargetRefusal(
        f"0x{address:08X} is not a NAMED target. This pipeline decompiles a work order, not an "
        f"address space: the named set is {[str(t) for t in (CONTROL,) + CHAIN]}. An unnamed "
        "address is not refused as a bad address, it is refused as out of scope -- which is what "
        "keeps the corpus bounded.")
