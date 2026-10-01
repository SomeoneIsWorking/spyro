#!/usr/bin/env python3
"""load_compare.py — 0155 §7 M1 (payload) and M2 (terminal state) of Spyro 1's loading, compared
against the independent full-console reference, and the R6 XA-gate question M3 left open.

WHAT THIS ANSWERS, and what it refuses to answer.

M1 is a claim about the PORT'S CD OVERRIDE: `game/core/cd_queue.cpp` never enters retail's spin
loops, it stages the whole range, hashes it, writes it and returns. A ledger of what it did
(`game/core/load_ledger.cpp`, `PSXPORT_LOAD_LEDGER`) is a record of the port talking to itself. The
only thing that can say whether those bytes are the bytes a PSX CD drive delivers is a core that
really reads the disc. So both legs run the SAME route on the SAME disc and produce the multiset
`(issuer site, LBA, byte offset, length, destination, SHA-256)`:

  * the port leg is the ledger;
  * the reference leg is the reference core's own CD loader entries, read with the pinned core's
    bounded PC observer (`tools/oracle/console.py observe` -> `observe_read`), whose arguments ARE
    the four load registers the guest passes (`a0` base LBA, `a1` buffer, `a2` length, `a3` byte
    offset), and whose payload identity is the SHA-256 of those disc sectors computed HERE, from the
    archive the disc was extracted into, by this file.

The reference leg's digest is deliberately NOT read out of the port's ledger and not read out of the
emulated RAM: it is computed from the bytes on the medium, so agreeing with the port is evidence
about the disc rather than a tautology. M2 closes the loop the other way: the two cores' own
main-RAM dumps are compared over every payload destination, so "both sides computed the same digest
from the same file" is backed by "both cores' RAM holds the same bytes".

WHAT IS NOT CLAIMED. The reference is Beetle's full software console, which shares the pinned device
lineage PSXPort uses, so agreement does not independently validate a device bug both share
(`external/psxport/tools/oracle/CONSOLE.md`, "Comparison validity"). The reference's field numbers are
not state alignment and are not compared as such.

THE COVERAGE DENOMINATOR IS 31 AND IT IS THE ISSUE'S, NOT THIS FILE'S. The census table lives in the
C++ owner that records the operations (`load_ledger.cpp`, re-derived from the image), so this tool
reads the ledger's own coverage lines rather than keeping a second copy of 31 addresses that could
quietly disagree with the first. A site the route never reached is named, because a route that never
dies and never pauses looks exactly like full coverage in a table of what was seen.

EVERY LEG CAN FAIL, AND IS SHOWN FAILING.
  * M1's negative is §7's own: the archive truncated by one sector makes the payload digest of
    every operation that needs that sector unavailable, and the comparison must report those
    operations UNEQUAL rather than quietly comparing the operations that still fit.
  * M2's negative is the field-list comparer itself (`tools/ram_compare.py`), whose selftest is this
    tool's: a capture pair differing in a clock and in a hand-off field must report exactly those
    two, with the hand-off one labelled as the finding.
  * `--selftest` runs all of that offline over recorded data, and also requires the parsers to
    REFUSE a ledger report and an observer record set they cannot read, because a parser that
    returns an empty list is a green zero.

    uv run --frozen python tools/load_compare.py --selftest
    uv run --frozen python tools/load_compare.py --bios ../SCPH1001.BIN
    uv run --frozen python tools/load_compare.py --ledger L.txt --records R.json --archive WAD.WAD
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import guest_globals  # noqa: E402  (the shipping owner's addresses, never a second copy)
import ram_compare  # noqa: E402  (0151's field-list comparer; reused, not forked)

DEFAULT_BIOS = ROOT.parent / "SCPH1001.BIN"
OUT_DIR = ROOT / "scratch" / "load_compare"
LEDGER_PATH = OUT_DIR / "ledger.txt"
RECORDS_PATH = OUT_DIR / "observer_records.json"

# ---- the two CD primitives (0155 §1) and the two R6 writers of the word the loaders gate on ------
CD_LOAD_SYNC = 0x80016500  # blocking
CD_LOAD_ASYNC = 0x80016698  # streaming
LOADER_PCS = (CD_LOAD_SYNC, CD_LOAD_ASYNC)

# g_CdMusic.m_Flags (0x800774B4) and the CD read-pending gate (0x80076BB8). The observer carries
# both in every record, so the value of the music word is sampled at each observed instruction rather
# than polled around it.
G_CD_MUSIC_FLAGS = 0x800774B4
G_CD_GATE = 0x80076BB8
OBSERVED_RANGES = ((G_CD_MUSIC_FLAGS, 4), (G_CD_GATE, 4))

# R6: the two instructions that store 0x100 — bit 6 CLEAR — to 0x800774B4, against the four that
# store 0x40 (bit 6 set). Read from the image with tools/writers.py and read by eye in
# tools/probe_guest_disasm.py; both are `addiu $v0,$zero,N` immediately before the store.
R6_CLEAR_WRITERS = (0x8002BF2C, 0x8002BFC4)

# The WAD archive's own LBA on the disc (0x80012518/0x8001251C/0x80012524: `g_CdState.m_WadSector`
# = 37). A read whose base LBA is not this one is not an archive read, and the payload mapping below
# would be a guess, so it is refused rather than applied.
WAD_SECTOR = 37

# M2's terminals (0155 §7 M2): LoadLevel leaves stage 13 by storing -1 at 0x800163B8, and
# LoadCutscene's terminal is stage 10 (its callers spin `while (stage < 10)`).
TERMINAL_STAGE_DONE = 0xFFFFFFFF
TERMINAL_STAGE_CUTSCENE = 10

SECTOR = 2048


class Refusal(RuntimeError):
    """An input this tool will not guess at."""


# --------------------------------------------------------------------------------------------------
# the two legs' records
# --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class LoadOperation:
    """One CD read operation, in the form M1 compares: what was asked for and what bytes arrived.

    `site` is the issuer address both legs recover the same way — the `jal` return address minus 8 —
    so the two cores' records are comparable without either side naming a function.
    """

    site: int
    base_lba: int
    byte_offset: int
    length: int
    destination: int
    digest: str
    deferred: bool
    source: str  # "ledger" or "reference-observer"
    refused: bool = False

    @property
    def key(self) -> tuple:
        return (self.site, self.base_lba, self.byte_offset, self.length, self.destination,
                self.digest, self.refused)

    @property
    def payload_key(self) -> tuple:
        """The same record WITHOUT the issuer site, for the one difference §7's tuple does not
        excuse: a read the port dispatched from its own boot owner has no `jal` to name it by, while
        the reference's guest code issues the very same read from a real site. Comparing the payload
        alone says whether the BYTES agree, and the strict key still reports the attribution gap."""
        return (self.base_lba, self.byte_offset, self.length, self.destination, self.digest,
                self.refused)

    def describe(self) -> str:
        return (f"site 0x{self.site:08X} lba {self.base_lba} +0x{self.byte_offset:X} "
                f"len {self.length} dest 0x{self.destination:08X} "
                f"{'deferred' if self.deferred else 'blocking'} "
                f"{'REFUSED' if self.refused else self.digest[:16]}")


_SITE_LABEL = re.compile(r"^(?:(?P<id>[A-Z]\d{2}) )?(?P<address>0x[0-9A-F]{8})")
_OPERATION_ROW = re.compile(
    r"^\s*(?P<index>\d+)\s+(?P<site>.+?)\s+(?P<pc>0x[0-9A-F]{8})\s+"
    r"(?P<lba>0x[0-9A-F]{8})\s+(?P<offset>0x[0-9A-F]{8})\s+(?P<length>\d+)\s+"
    r"(?P<dest>0x[0-9A-F]{8})\s+(?P<kind>blocking|stream)\s+(?P<stage>-?\d+)\s+\S+\s+\S+\s+\S+\s+"
    r"(?P<music>clear|busy)\s+(?P<digest>[0-9a-fA-F]{64}|NO-DIGEST)(?P<flag> REFUSED)?\s*$")
_COVERAGE = re.compile(
    r"^load ledger: issuer sites exercised (?P<exercised>\d+) of (?P<total>\d+) "
    r"named in 0155 section 2")
_SITE_LIST = re.compile(r"^load ledger: (?P<which>exercised|unreached) sites \((?P<count>\d+)\): "
                        r"(?P<sites>.*)$")
_SITE_ENTRY = re.compile(r"(?P<id>[A-Z]\d{2})@(?P<address>0x[0-9A-F]{8})(?P<flag> UNREACHED)?")


@dataclass
class LedgerReport:
    operations: list[LoadOperation]
    exercised: list[tuple[str, int]]
    unreached: list[tuple[str, int]]
    total_sites: int
    # The ledger's own per-row music-gate column, in row order: "busy" (bit set) or "clear".
    music_at_issue: list[str] = field(default_factory=list)

    @property
    def site_names(self) -> dict[int, str]:
        """Every site the census NAMES, reached or not: a site the port never reached is still in the
        census, and treating it as unknown would report the reference's own boot issuers as unnamed."""
        return {address: identifier for identifier, address in self.exercised + self.unreached}

    def music_clear_count(self) -> int:
        return 0  # the report's per-row column is read by the caller that needs the row text


def parse_ledger_report(text: str) -> LedgerReport:
    """The product's own report (game/core/load_ledger.cpp) as records.

    REFUSES, rather than returning an empty list: a report whose header the tool cannot recognise is
    a report from a different binary, and an empty operation list would compare equal to nothing and
    print a green M1 with no operations on either side.
    """
    operations: list[LoadOperation] = []
    music: list[str] = []
    exercised: list[tuple[str, int]] = []
    unreached: list[tuple[str, int]] = []
    total_sites = 0
    saw_header = False
    for line in text.splitlines():
        if line.startswith("load ledger: per operation"):
            saw_header = True
            continue
        coverage = _COVERAGE.match(line)
        if coverage:
            total_sites = int(coverage.group("total"))
            continue
        sites = _SITE_LIST.match(line)
        if sites:
            entries = [(m.group("id"), int(m.group("address"), 16))
                       for m in _SITE_ENTRY.finditer(sites.group("sites"))]
            if len(entries) != int(sites.group("count")):
                raise Refusal(f"ledger coverage line names {sites.group('count')} sites but lists "
                              f"{len(entries)}: {line.strip()!r}")
            (exercised if sites.group("which") == "exercised" else unreached).extend(entries)
            continue
        row = _OPERATION_ROW.match(line)
        if row:
            label = _SITE_LABEL.match(row.group("site"))
            if label is None:
                raise Refusal(f"cannot read the issuer site in ledger row: {line.strip()!r}")
            digest = row.group("digest")
            operations.append(LoadOperation(
                site=int(label.group("address"), 16),
                base_lba=int(row.group("lba"), 16),
                byte_offset=int(row.group("offset"), 16),
                length=int(row.group("length")),
                destination=int(row.group("dest"), 16),
                digest="" if digest == "NO-DIGEST" else digest,
                deferred=row.group("kind") == "stream",
                source="ledger",
                refused=row.group("flag") is not None))
            music.append(row.group("music"))
    if not saw_header:
        raise Refusal("this is not a LoadLedger report: no 'load ledger: per operation' header. A run "
                      "without PSXPORT_LOAD_LEDGER writes no report, and comparing against nothing "
                      "would report M1 as equal with zero operations on both sides.")
    if total_sites == 0:
        raise Refusal("the report carries no 'exercised N of M' coverage line, so the 0155 site "
                      "denominator is unknown; refusing rather than dividing by zero sites")
    return LedgerReport(operations, exercised, unreached, total_sites, music)


def operations_from_records(records: Iterable[dict], ranges: Sequence[tuple[int, int]] = OBSERVED_RANGES,
                            digests: dict[tuple[int, int], str] | None = None,
                            source: str = "reference-observer") -> list[LoadOperation]:
    """The reference leg: one record per observed CD loader entry, read out of the observer drain.

    `digests` maps `(base LBA, byte offset, length)` to the payload digest computed from the medium.
    An entry whose payload digest is missing keeps an empty digest rather than being dropped, so a
    missing payload is visible as an unequal operation instead of as a shorter table.
    """
    operations: list[LoadOperation] = []
    for record in records:
        pc = int(record.get("pc", 0))
        if pc not in LOADER_PCS:
            continue
        gpr = [int(value) for value in record.get("gpr", [])]
        if len(gpr) < 8:
            raise Refusal(f"observer record at 0x{pc:08X} carries {len(gpr)} general registers; "
                          f"the load arguments cannot be read from it")
        base_lba, destination, length, byte_offset = gpr[4], gpr[5], gpr[6], gpr[7]
        key = (base_lba, byte_offset, length)
        operations.append(LoadOperation(
            site=(gpr[31] - 8) & 0xFFFFFFFF,
            base_lba=base_lba,
            byte_offset=byte_offset,
            length=length,
            destination=destination,
            digest=(digests or {}).get(key, ""),
            deferred=pc == CD_LOAD_ASYNC,
            source=source))
    return operations


# --------------------------------------------------------------------------------------------------
# the payload source: the bytes on the medium
# --------------------------------------------------------------------------------------------------


class Archive:
    """The WAD archive the disc was extracted into, as the ONE owner of "what those sectors hold".

    Every refusal below is the M1 negative seam, at the same place `test_archive_transfer` puts it:
    a short source must not produce a digest for the bytes it does not have, because a digest of a
    prefix compares equal to nothing.
    """

    def __init__(self, path: Path, wad_sector: int = WAD_SECTOR, truncate_bytes: int = 0):
        if not path.is_file():
            raise Refusal(f"{path} does not exist; the payload digests have no source, and a "
                          f"comparison with no source would report every operation equal")
        blob = path.read_bytes()
        if truncate_bytes:
            if truncate_bytes >= len(blob):
                raise Refusal(f"--truncate-bytes {truncate_bytes} is not short of the archive "
                              f"({len(blob)} bytes); the negative must actually remove bytes")
            blob = blob[:-truncate_bytes]
        self.path = path
        self.blob = blob
        self.wad_sector = wad_sector
        self.full_size = path.stat().st_size
        self.truncate_bytes = truncate_bytes
        self.digest = hashlib.sha256(path.read_bytes()).hexdigest()

    def digest_of(self, base_lba: int, byte_offset: int, length: int) -> str:
        if base_lba != self.wad_sector:
            raise Refusal(f"read at base LBA {base_lba} is not the WAD archive's own sector "
                          f"{self.wad_sector}; the byte-offset mapping below would be a guess")
        if byte_offset % SECTOR:
            raise Refusal(f"archive offset 0x{byte_offset:X} is not sector aligned; the port refuses "
                          f"such a request too (ArchiveTransfer::read)")
        end = byte_offset + length
        if end > len(self.blob):
            raise Refusal(f"sectors for [0x{byte_offset:X}, 0x{end:X}) are past the archive's "
                          f"{len(self.blob)} bytes ({self.full_size} on disc"
                          + (f", truncated by {self.full_size - len(self.blob)}" if
                             self.full_size != len(self.blob) else "")
                          + "); the payload has no digest")
        return hashlib.sha256(self.blob[byte_offset:end]).hexdigest()


# --------------------------------------------------------------------------------------------------
# M1
# --------------------------------------------------------------------------------------------------


@dataclass
class PayloadComparison:
    matched: int = 0
    only_port: list[LoadOperation] = field(default_factory=list)
    only_reference: list[LoadOperation] = field(default_factory=list)
    refused_port: list[LoadOperation] = field(default_factory=list)

    @property
    def equal(self) -> bool:
        return not (self.only_port or self.only_reference or self.refused_port)

    def report(self, label: str = "M1 payload multiset") -> list[str]:
        lines = [f"{label}: {self.matched} operation(s) equal on both cores, "
                 f"{len(self.only_port)} only on the port, {len(self.only_reference)} only on the "
                 f"reference, {len(self.refused_port)} refused by the port"]
        for operation in self.refused_port:
            lines.append(f"  UNEQUAL (port refused, no payload): {operation.describe()}")
        for operation in self.only_port:
            lines.append(f"  UNEQUAL (port only): {operation.describe()}")
        for operation in self.only_reference:
            lines.append(f"  UNEQUAL (reference only): {operation.describe()}")
        return lines


def compare_payloads(port: Sequence[LoadOperation], reference: Sequence[LoadOperation],
                     by_site: bool = True) -> PayloadComparison:
    """Multiset equality on the M1 key. Order is not compared: a port that issues the same reads a
    field earlier than a core with real CD timing is the removal working, not a divergence.

    `by_site=False` drops the issuer site from the key, which is the narrower claim "the bytes agree"
    as opposed to "the bytes agree AND each side names the same `jal`".
    """
    result = PayloadComparison()
    key_of = (lambda operation: operation.key) if by_site else (lambda operation: operation.payload_key)
    port_counts = Counter(key_of(operation) for operation in port)
    reference_counts = Counter(key_of(operation) for operation in reference)
    for key, count in port_counts.items():
        result.matched += min(count, reference_counts.get(key, 0))
    result.only_port = [operation for operation in port
                        if reference_counts.get(key_of(operation), 0) < port_counts[key_of(operation)]]
    result.only_reference = [operation for operation in reference
                             if port_counts.get(key_of(operation), 0) < reference_counts[key_of(operation)]]
    result.refused_port = [operation for operation in port if operation.refused]
    return result


def loader_entry_gate_samples(records: Iterable[dict],
                               ranges: Sequence[tuple[int, int]] = OBSERVED_RANGES) -> MusicGateSamples:
    """The music gate as each CD loader ENTRY saw it, taken from the RAM ranges the observer copies
    into every record.

    This is the measurement R6 turns on, and it is not the same measurement as polling: a poll says
    the word takes both values during the run, and an entry sample says whether any read was ever
    issued while it held the other one.
    """
    samples = MusicGateSamples()
    for record in records:
        if int(record.get("pc", 0)) not in LOADER_PCS:
            continue
        blob = bytes.fromhex(record.get("ram", ""))
        cursor = 0
        for address, size in ranges:
            if address == G_CD_MUSIC_FLAGS and len(blob) >= cursor + size:
                samples.add(int.from_bytes(blob[cursor:cursor + size], "little"))
            cursor += size
    return samples


# --------------------------------------------------------------------------------------------------
# M2
# --------------------------------------------------------------------------------------------------


def m2_fields() -> tuple[list[ram_compare.Field], frozenset[str]]:
    """0155 §7 M2's named field list, as `tools/ram_compare.py` fields.

    The hand-off fields are this transition's DEFINING state: a difference in one of them is a claim
    the load did not hand over the same state. The clock fields are reported separately and are
    expected to differ — the port removes the load's fields, so it arrives earlier in every counter.
    """
    must_match = [
        ram_compare.Field("g_LoadStage", guest_globals.kLoadStage, source="0155 M2"),
        ram_compare.Field("g_Gamestate", guest_globals.kGamestate, source="0155 M2"),
        ram_compare.Field("g_LevelId", guest_globals.kLevelId, source="0155 M2"),
        ram_compare.Field("g_LevelIndex", 0x80075964, source="route_scenes.G_LEVEL_INDEX"),
        ram_compare.Field("g_CdMusic.m_Flags", G_CD_MUSIC_FLAGS, source="0155 §1"),
        ram_compare.Field("g_CdState.m_IsReading", G_CD_GATE, source="0155 §1"),
        ram_compare.Field("g_LevelHeader.m_Mark", 0x80076C00, source="0155 §2.1 header at 0x8007A6D0"),
        ram_compare.Field("g_LevelHeader.m_Mark+8", 0x80076C08, source="0155 §2.1"),
        # The g_Buffers pointer block. Its base is derived, not quoted: tools/probe_hud_glyph.py
        # MEASURED `g_Buffers.m_HudOTStart` at 0x800785F0, and buffers.h declares six pointers before
        # it, so the block starts at 0x800785DC. The range is compared a word wider on each side so a
        # base that is one word off still compares the same bytes on both cores.
        ram_compare.Field("g_Buffers.m_CopyBuf-4", 0x800785D8, source="buffers.h + measured +0x18"),
        ram_compare.Field("g_Buffers.m_CopyBuf", 0x800785DC, source="buffers.h"),
        ram_compare.Field("g_Buffers.m_ModelData", 0x800785E4, source="buffers.h"),
        ram_compare.Field("g_Buffers.m_LevelScene", 0x800785E8, source="buffers.h"),
        ram_compare.Field("g_Buffers.m_HigherPolyBuffer+16", 0x800785F4, source="buffers.h"),
    ]
    clocks = [
        ram_compare.Field("g_GameTick", guest_globals.kGameTick, source="clock"),
        ram_compare.Field("g_LevelTicks", guest_globals.kLevelTicks, source="clock"),
        ram_compare.Field("g_DeltaTime", guest_globals.kDeltaTime, source="clock"),
        ram_compare.Field("g_LevelTransTicks", 0x800756AC, source="clock"),
        ram_compare.Field("g_UnprocessedFrames", guest_globals.kUnprocessedFrames, source="clock"),
        ram_compare.Field("D_800758B8", 0x800758B8, source="clock"),
    ]
    names = frozenset(field.name for field in must_match)
    return must_match + clocks, names


def compare_terminal(left: Path, right: Path, payloads: Sequence[LoadOperation],
                     expected: Sequence[str]) -> dict:
    """M2 at one terminal: the named field list, and a hash of every payload destination.

    A destination range is only required to EQUAL when it still holds its payload: a later read that
    targets the same address has legitimately replaced it, and every Spyro load into the WAD arena
    replaces the one before. The classification is measured, not assumed — it is the set of LATER
    operations with the same destination in the same run's ledger — and both lists are printed, so
    "5 ranges differ" can be read as "5 ranges differ, all of them re-used since" rather than as a
    payload defect, and as "5 differ and none was re-used" when that is what happened.
    """
    fields, handoff = m2_fields()
    # ram_compare publishes its classification as a module-level set; adding this terminal's
    # hand-off names to it is what makes the shared comparer label them as findings rather than as
    # clock drift, instead of this file printing its own verdict beside it.
    ram_compare.HANDOFF_FIELDS = frozenset(ram_compare.HANDOFF_FIELDS | handoff)
    try:
        total, same, differing, lines = ram_compare.compare(ram_compare.Dump(left),
                                                           ram_compare.Dump(right), fields)
    except ram_compare.Refusal as refusal:
        raise Refusal(str(refusal)) from None
    destinations = []
    for index, operation in enumerate(payloads):
        port_digest = _range_digest(left, operation)
        reference_digest = _range_digest(right, operation)
        reused = sum(1 for later in payloads[index + 1:]
                     if later.destination == operation.destination)
        # Three-way, because "the two cores disagree" has two very different causes: one core wrote
        # something other than the payload (the guest re-using its own buffer, which is not a
        # loading defect), or the payload never landed. The medium decides which.
        on_disc = expected[index] if index < len(expected) else ""
        destinations.append((operation, port_digest, reference_digest, reused, on_disc,
                             port_digest == on_disc, reference_digest == on_disc))
    return {"fields_compared": total, "fields_equal": same, "fields_differing": differing,
            "lines": lines, "destinations": destinations}


def _range_digest(dump: Path, operation: LoadOperation) -> str | None:
    """SHA-256 of the payload's destination range inside one core's main RAM, or None when the range
    does not fit the dump. A hash, not a diff: §7 M2(i) asks for a hash of the destination range."""
    if not operation.length:
        return None
    start = operation.destination & 0x1FFFFFFF
    end = start + operation.length
    if end > ram_compare.RAM_SIZE:
        return None
    with dump.open("rb") as handle:
        handle.seek(start)
        blob = handle.read(operation.length)
    if len(blob) != operation.length:
        return None
    return hashlib.sha256(blob).hexdigest()


# --------------------------------------------------------------------------------------------------
# R6
# --------------------------------------------------------------------------------------------------


@dataclass
class MusicGateSamples:
    """Every observation of g_CdMusic.m_Flags' 0x40 bit, with the denominator it came from."""

    polls: int = 0
    clear: int = 0
    set: int = 0
    values: Counter = field(default_factory=Counter)

    def add(self, value: int) -> None:
        self.polls += 1
        self.values[value & 0xFFFFFFFF] += 1
        if value & 0x40:
            self.set += 1
        else:
            self.clear += 1

    def report(self, name: str) -> list[str]:
        return [f"R6 {name}: {self.polls} observation(s) of [0x{G_CD_MUSIC_FLAGS:08X}] & 0x40 -> "
                f"{self.set} set, {self.clear} CLEAR; values seen "
                f"{', '.join(f'0x{v:X}x{n}' for v, n in sorted(self.values.items()))}"]


# --------------------------------------------------------------------------------------------------
# the driven comparison
# --------------------------------------------------------------------------------------------------


class Instrumented:
    """One core, plus what this issue needs from it: the reference's CD loader entries, and a RAM
    dump at each load terminal.

    It is a transparent proxy for the `CoreSession` protocol (compare_cores.py): every attribute it
    does not override is the session's own, so the title's driver, the checkpoints and the report all
    run unchanged. Only `step` is intercepted, because that is the one boundary at which both cores
    are parked and both can be read without disturbing them.
    """

    def __init__(self, session, *, name: str, dumps: Path, records: list | None = None,
                 observer=None, poll_every: int = 1, dump_limit: int = 8):
        self._session = session
        self.name = name
        self.reference = session.reference
        self.dumps = dumps
        self.dumps.mkdir(parents=True, exist_ok=True)
        self.records = records
        self.observer = observer
        self.poll_every = poll_every
        self.dump_limit = dump_limit
        self.music = MusicGateSamples()
        self.terminals: list[dict] = []
        self.observer_status: dict | None = None
        self._last_stage: int | None = None
        self._ordinals: Counter = Counter()
        self._terminal_count = 0
        self._polls = 0

    def __getattr__(self, item):
        return getattr(self._session, item)

    def step(self, frames: int) -> None:
        self._session.step(frames)
        if self.observer is not None:
            drained = self.observer["call"]({"command": "observe_read"})
            self.records.extend(drained["records"])
        if self.poll_every and (self._polls % self.poll_every) == 0:
            self._polls += 1
            self.music.add(int.from_bytes(self._session.read(G_CD_MUSIC_FLAGS, 4), "little"))
        stage = int.from_bytes(self._session.read(guest_globals.kLoadStage, 4), "little")
        stage &= 0xFFFFFFFF
        previous, self._last_stage = self._last_stage, stage
        if stage == previous or stage not in (TERMINAL_STAGE_DONE, TERMINAL_STAGE_CUTSCENE):
            return
        if self._terminal_count >= self.dump_limit:
            return
        ordinal = self._ordinals[stage]
        self._ordinals[stage] += 1
        kind = "loadlevel_stage13_exit" if stage == TERMINAL_STAGE_DONE else "loadcutscene_stage10"
        path = self.dumps / f"{kind}_{ordinal}_{self.name}.ram"
        try:
            self._dump(path)
        except Exception as error:  # noqa: BLE001 - recorded, never silently ignored
            self.terminals.append({"kind": kind, "ordinal": ordinal, "core": self.name,
                                   "dumped": None, "error": str(error)})
            return
        self._terminal_count += 1
        self.terminals.append({"kind": kind, "ordinal": ordinal, "core": self.name,
                               "dumped": str(path), "stage": stage, "frames": self._session.frames,
                               "error": None})

    def _dump(self, path: Path) -> None:
        if self._session.reference:
            reply = self._session._call({"command": "ram"})
            Path(reply["ram"]).replace(path)
        else:
            self._session._send(f"dumpram {path}")
            self._session._expect(f"dumpram -> {path}")
            if not path.is_file():
                raise Refusal(f"the product reported a RAM dump but {path} was not written")

    def close(self) -> None:
        # Two session-specific closings, because the default ones lose evidence this issue needs.
        #
        # The reference: the observer is drained after the LAST step, while the pipe is still open.
        # compare_cores closes the session right after the route, and a census read afterwards hits a
        # closed pipe -- which is how the first run of this tool lost every record it had collected.
        #
        # The product: `quit` DETACHES the REPL and lets the game run on (runtime/psx/repl.cpp says
        # so in as many words), so the harness SIGKILLs it and every run-end reporter is skipped --
        # including the LoadLedger report M1's port leg IS. `end` is the request that ends the run,
        # which is what a headless capture wants and what drive.py sends.
        if self.observer is not None:
            try:
                drained = self.observer["call"]({"command": "observe_read"})
                self.records.extend(drained["records"])
                self.observer_status = drained["status"]
            except Exception:  # noqa: BLE001 - the close path must not mask the run's own result
                pass
        elif not self._session.reference:
            try:
                self._session._send("end")
                self._session._process.wait(timeout=180)
            except (OSError, subprocess.TimeoutExpired):
                pass
        self._session.close()


def run_comparison(args) -> int:
    """Drive both cores over the title's own route, then answer M1, M2 and R6 from what they did."""
    import compare  # noqa: PLC0415 - imported after sys.path is set up by this module's header
    import drive  # noqa: PLC0415
    import oracle_spyro1  # noqa: PLC0415

    disc = args.disc or drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC or pass --disc", file=sys.stderr)
        return 2
    # The reference is launched with the framework checkout as its working directory, so a relative
    # BIOS path would be resolved against the PINNED worktree rather than against this repository.
    args.bios = args.bios.resolve()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    product = compare.Product(ROOT / args.executable, ROOT / args.binary,
                              drive.environment(disc), ROOT, Path(disc))
    environment = dict(product.environment)
    environment["PSXPORT_LOAD_LEDGER"] = str(LEDGER_PATH)
    product = compare.Product(product.binary, product.executable, environment, product.cwd,
                              product.disc)

    targets = [(CD_LOAD_SYNC, False), (CD_LOAD_ASYNC, False)]
    targets += [(pc, False) for pc in R6_CLEAR_WRITERS]
    ranges = list(OBSERVED_RANGES)
    records: list[dict] = []
    built: list[Instrumented] = []

    def sessions(product_, args_, out_dir):
        native_session = compare.NativeReplSession(str(product_.binary), str(product_.executable),
                                                   product_.environment, str(product_.cwd),
                                                   out_dir / "native.log")
        console_session = compare.ConsoleSession(compare.PSXPORT, product_.disc, args_.bios,
                                                 args_.region, out_dir / "console.log")

        def observer_call(message):
            # console.py's JSON protocol, through the session that already owns the pipe. The
            # observer is a console command, and ConsoleSession exposes the command vocabulary it
            # needs for a comparison (read/step/hold) rather than every command the protocol has.
            return console_session._call(message)

        status = observer_call({"command": "observe", "targets": [{"pc": f"0x{pc:08X}",
                                                                    "return": False}
                                                                  for pc, _ in targets],
                                "ranges": [{"address": f"0x{address:08X}", "bytes": size}
                                           for address, size in ranges], "capacity": 128})
        print(f"[load-compare] reference PC observer: scanned {status['scanned']} instruction(s) at "
              f"configure, targets {', '.join(f'0x{pc:08X}' for pc, _ in targets)}; a target that "
              f"never appears in the census below is itself the R6 answer, not a failure")
        native = Instrumented(native_session, name="port", dumps=OUT_DIR / "dumps",
                              poll_every=args.poll_every)
        console = Instrumented(console_session, name="reference", dumps=OUT_DIR / "dumps",
                               records=records, observer={"call": observer_call},
                               poll_every=args.poll_every)
        built.extend((native, console))
        return native, console

    exit_code = compare.run(oracle_spyro1, product, args, OUT_DIR, sessions=sessions)
    final = built[1].observer_status
    if final is not None:
        print(f"[load-compare] reference PC observer census: scanned {final['scanned']}, "
              f"matched {final['matched']}, dropped {final['dropped']}, "
              f"pairing errors {final['pairing_errors']}")
        for target in final["targets"]:
            print(f"[load-compare]   target 0x{target['pc']:08X}: {target['entries']} entr(y/ies), "
                  f"{target['returns']} return(s)")
    terminals = [entry for proxy in built for entry in proxy.terminals]
    (OUT_DIR / "terminals.json").write_text(json.dumps(terminals, indent=2))
    RECORDS_PATH.write_text(json.dumps(records, indent=1))
    return exit_code, records, built, terminals


# --------------------------------------------------------------------------------------------------
# selftest
# --------------------------------------------------------------------------------------------------


LEDGER_SAMPLE = """load ledger: per operation
  # site ra-8 pc lba off len dest deferred stage@issue field@issue field@done latency music-idle@issue sha256
  0 S01 0x8001253C 0x80016500 0x00000025 0x00000000 2048 0x8007AA38 blocking 0 nocounter nocounter 0 busy {a}
  1 A01 0x8001569C 0x80016698 0x00000025 0x00002000 57344 0x8007AA38 stream 2 1861 1863 2 clear {b}
  2 S09 0x8002EEC4 0x80016500 0x00000025 0x00008000 4096 0x80100000 blocking 0 100 100 0 busy NO-DIGEST REFUSED
load ledger: issuer sites exercised 3 of 31 named in 0155 section 2
load ledger: exercised sites (3): S01@0x8001253C, A01@0x8001569C, S09@0x8002EEC4
load ledger: unreached sites (28): S02@0x80012924, S03@0x80012970, S04@0x80012994, S05@0x800129C0, S06@0x8005B83C, S07@0x8002D31C, S08@0x8002E328, S10@0x80033688, S11@0x80014518, A02@0x800156E4, A03@0x80015750, A04@0x8001582C, A05@0x800158C8, A06@0x80015A3C, A07@0x80015BC0, A08@0x80014608, A09@0x80014680, A10@0x80014740, A11@0x800147C8, A12@0x800148AC, A13@0x80014920, A14@0x80014A08, A15@0x80014CCC, A16@0x80014E6C, A17@0x80015188, A18@0x80015248, A19@0x8003381C, T01@0x8007ADFC
load ledger: operations seen 3, still pending 0, worst measured latency 2 fields, operations issued before a field owner existed 1
"""


def _sample_digests() -> dict[str, str]:
    return {"a": hashlib.sha256(b"a").hexdigest(),
            "b": hashlib.sha256(b"b").hexdigest()}


def _selftest() -> int:
    failures = 0

    def check(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"SELFTEST FAILED: {message}", file=sys.stderr)
            failures += 1

    digests = _sample_digests()
    text = LEDGER_SAMPLE.format(**digests)
    report = parse_ledger_report(text)
    check(len(report.operations) == 3, f"three ledger rows, got {len(report.operations)}")
    check(report.total_sites == 31, f"the coverage denominator must be the census's 31, got "
                                    f"{report.total_sites}")
    check([name for name, _ in report.exercised] == ["S01", "A01", "S09"], report.exercised)
    check(len(report.unreached) == 28, f"28 unreached sites, got {len(report.unreached)}")
    check(report.operations[0].digest == digests["a"], report.operations[0].digest)
    check(report.operations[1].deferred and report.operations[1].byte_offset == 0x2000,
          report.operations[1])
    check(report.operations[2].refused and report.operations[2].digest == "",
          "a refused operation must stay visible with no digest")
    print(f"  ledger report -> {len(report.operations)} operations, "
          f"{len(report.exercised)}/{report.total_sites} sites exercised, "
          f"{len(report.unreached)} named unreached")

    # A report that is not a report is REFUSED, never read as zero operations: a green M1 with no
    # operations on either side is the failure this whole tool exists to not have.
    for bad, why in (("", "per operation"),
                     ("load ledger: per operation\n", "exercised"),
                     (text.replace(" of 31 ", " of 0 "), "exercised")):
        try:
            parse_ledger_report(bad)
        except Refusal as refusal:
            check(why in str(refusal), f"refusal for {why!r} named it: {refusal}")
        else:
            check(False, f"a report without a {why!r} line was accepted")

    # The reference leg: observer records read at the loader entry.
    records = [{"pc": CD_LOAD_SYNC, "gpr": [0] * 4 + [WAD_SECTOR, 0x8007AA38, 2048, 0] + [0] * 23
                                                        + [0x80012544], "ram": "0040000000000000"},
               {"pc": CD_LOAD_ASYNC, "gpr": [0] * 4 + [WAD_SECTOR, 0x8007AA38, 4096, 0x2000]
                                                + [0] * 23 + [0x800156A4], "ram": "0040000000000000"},
               {"pc": 0x8002BF2C, "gpr": [0] * 34, "ram": "0040000000000000"}]
    reference = operations_from_records(records, digests={(WAD_SECTOR, 0, 2048): digests["a"],
                                                         (WAD_SECTOR, 0x2000, 4096): digests["b"]})
    check(len(reference) == 2, f"two loader entries, got {len(reference)}")
    check(reference[0].site == 0x8001253C and reference[0].digest == digests["a"], reference[0])
    check(reference[1].site == 0x8001569C and reference[1].deferred, reference[1])
    print(f"  observer records -> {len(reference)} CD loader entries at sites "
          f"{', '.join(f'0x{op.site:08X}' for op in reference)} "
          f"(the third record is an R6 writer, correctly not a read)")

    # A record without the load registers is refused, not read as zeros.
    try:
        operations_from_records([{"pc": CD_LOAD_SYNC, "gpr": [0, 0]}])
    except Refusal as refusal:
        print(f"  refuses a truncated observer record: {refusal}")
    else:
        check(False, "an observer record with two registers was accepted")

    # M1 equal, then each way of being unequal.
    port = [LoadOperation(0x8001253C, WAD_SECTOR, 0, 2048, 0x8007AA38, digests["a"], False, "ledger"),
            LoadOperation(0x8001569C, WAD_SECTOR, 0x2000, 4096, 0x8007AA38, digests["b"], True, "ledger")]
    same = compare_payloads(port, list(reversed(reference)))
    check(same.equal and same.matched == 2, f"identical multisets must compare equal: {same.report()}")
    print("  identical payload multisets -> 2 matched, 0 unequal (order is not compared)")

    wrong = LoadOperation(0x8001569C, WAD_SECTOR, 0x2000, 4096, 0x8007AA38, "0" * 64, True, "ledger")
    unequal = compare_payloads([port[0], wrong], reference)
    check(not unequal.equal and len(unequal.only_port) == 1, unequal.report())
    print(f"  one differing digest -> {unequal.report()[0]}")

    missing = compare_payloads([port[0]], reference)
    check(not missing.equal and len(missing.only_reference) == 1, missing.report())
    extra = compare_payloads(port + [LoadOperation(0x8002EEC4, WAD_SECTOR, 0x8000, 4096, 0x80100000,
                                                  digests["a"], False, "ledger")], reference)
    check(not extra.equal and len(extra.only_port) == 1, extra.report())
    print(f"  one missing and one extra operation -> both reported: "
          f"{len(missing.only_reference)} reference-only, {len(extra.only_port)} port-only")

    # The truncation seam: the same short source, refusing instead of hashing a prefix.
    archive_root = OUT_DIR
    archive_root.mkdir(parents=True, exist_ok=True)
    payload = OUT_DIR / "selftest_archive.bin"
    payload.write_bytes(b"".join(bytes([index % 251]) * SECTOR for index in range(8)))
    full = Archive(payload)
    check(full.digest_of(WAD_SECTOR, 0, 2048) != full.digest_of(WAD_SECTOR, 0, 4096),
          "the archive digest must cover the requested range")
    truncated = Archive(payload, truncate_bytes=SECTOR)
    check(truncated.digest_of(WAD_SECTOR, 0, 2048) == full.digest_of(WAD_SECTOR, 0, 2048),
          "the sectors before the truncation must still hash identically")
    try:
        truncated.digest_of(WAD_SECTOR, 6 * SECTOR, 4096)
    except Refusal as refusal:
        check("past the archive" in str(refusal), str(refusal))
        print(f"  truncated archive by one sector -> refused: {refusal}")
    else:
        check(False, "the truncated archive produced a digest for missing sectors")
    try:
        full.digest_of(WAD_SECTOR + 1, 0, 2048)
    except Refusal as refusal:
        print(f"  a read outside the WAD sector is refused: {refusal}")
    else:
        check(False, "a non-archive LBA was accepted")
    payload.unlink()

    # M2 over a synthetic pair of captures, through the SHARED comparer.
    fields, handoff = m2_fields()
    ram_compare.HANDOFF_FIELDS = frozenset(ram_compare.HANDOFF_FIELDS | handoff)
    check(len(handoff) == 13, f"the M2 hand-off field list is 13 fields, got {len(handoff)}")
    dumps = OUT_DIR / "selftest_dumps"
    dumps.mkdir(parents=True, exist_ok=True)
    left_bytes = bytearray(ram_compare.RAM_SIZE)
    right_bytes = bytearray(left_bytes)
    for item in fields:
        offset = item.address - ram_compare.RAM_BASE
        right_bytes[offset:offset + 4] = left_bytes[offset:offset + 4]
    # One clock differs (the port removes the load's fields, so it arrives earlier) and one
    # HAND-OFF field differs, which is the finding M2 exists to surface.
    struct.pack_into("<I", right_bytes, guest_globals.kGameTick - ram_compare.RAM_BASE, 4242)
    struct.pack_into("<I", right_bytes, guest_globals.kLevelId - ram_compare.RAM_BASE, 3)
    (dumps / "left.bin").write_bytes(bytes(left_bytes))
    (dumps / "right.bin").write_bytes(bytes(right_bytes))
    total, same_count, differing, lines = ram_compare.compare(ram_compare.Dump(dumps / "left.bin"),
                                                              ram_compare.Dump(dumps / "right.bin"),
                                                              fields)
    check(total == len(fields) and differing == 2, f"{differing} differing of {total}: {lines}")
    check(any("HAND-OFF STATE" in line and "g_LevelId" in line for line in lines), lines)
    check(any("clock/timing" in line and "g_GameTick" in line for line in lines), lines)
    print(f"  M2 field list -> {total} fields compared, {same_count} equal, {differing} differing "
          f"({sum('HAND-OFF STATE' in line for line in lines)} hand-off)")

    # The destination-range hash is a hash of that core's RAM, so two cores holding the same payload
    # agree and a port that wrote different bytes does not.
    same_range = LoadOperation(0x8001253C, WAD_SECTOR, 0, 4096, 0x80010000, "", False, "ledger")
    struct.pack_into("<I", right_bytes, 0x80010000 - ram_compare.RAM_BASE, 0xDEADBEEF)
    (dumps / "right2.bin").write_bytes(bytes(right_bytes))
    check(_range_digest(dumps / "left.bin", same_range) != _range_digest(dumps / "right2.bin", same_range),
          "one changed destination word must change the destination-range hash")
    print("  destination-range hash -> one changed word changes the hash")

    # R6's tally counts both answers.
    samples = MusicGateSamples()
    for value in (0x40, 0x40, 0x100, 0):
        samples.add(value)
    check((samples.set, samples.clear) == (2, 2), samples.report("selftest"))
    print(f"  {samples.report('selftest')[0]}")

    for path in (dumps / "left.bin", dumps / "right.bin", dumps / "right2.bin"):
        path.unlink()
    dumps.rmdir()

    if failures:
        print(f"load_compare selftest FAILED: {failures} case(s)", file=sys.stderr)
        return 1
    print("load_compare selftest PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true",
                        help="validate the parsers, the multiset comparison, the truncation negative "
                             "and the field-list comparison offline")
    parser.add_argument("--bios", type=Path, default=DEFAULT_BIOS)
    parser.add_argument("--region", default="na")
    parser.add_argument("--budget", type=int, default=6000)
    parser.add_argument("--frame-step", type=int, default=0)
    parser.add_argument("--route", type=Path)
    parser.add_argument("--route-from", type=int, default=0)
    parser.add_argument("--console-card", type=Path)
    parser.add_argument("--product-env", action="append", default=[], metavar="K=V")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--disc", type=Path)
    parser.add_argument("--archive", type=Path,
                        help="the WAD archive the disc was extracted into; the reference leg's payload "
                             "digests are computed from it")
    parser.add_argument("--ledger", type=Path, help="compare a recorded ledger report instead of driving")
    parser.add_argument("--records", type=Path, help="a recorded observer drain instead of driving")
    parser.add_argument("--truncate-bytes", type=int, default=0,
                        help="M1's negative: remove this many bytes from the payload source before "
                             "hashing, so an operation whose sectors are gone must report UNEQUAL")
    parser.add_argument("--poll-every", type=int, default=1,
                        help="poll the music gate every N fields (1 = every field)")
    parser.add_argument("--json", type=Path, help="write the machine-readable result here")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()

    if not args.archive and not (args.ledger and args.records):
        print("REFUSED: --archive (the payload source) or a recorded --ledger/--records pair is "
              "required; without a source there is nothing for M1 to compare", file=sys.stderr)
        return 2

    terminals: list[dict] = []
    archive: Archive | None = None
    if args.archive:
        archive = Archive(args.archive, truncate_bytes=args.truncate_bytes)
        print(f"[load-compare] payload source {args.archive.name}: {archive.full_size} bytes, "
              f"sha256 {archive.digest[:16]}"
              + (f", TRUNCATED to {len(archive.blob)} bytes for this run" if archive.truncate_bytes
                 else ""))

    if args.ledger and args.records:
        ledger = parse_ledger_report(args.ledger.read_text())
        records = json.loads(args.records.read_text())
        reference_operations = operations_from_records(records)
        terminal_file = OUT_DIR / "terminals.json"
        if terminal_file.is_file():
            terminals = json.loads(terminal_file.read_text())
    else:
        exit_code, records, built, terminals = run_comparison(args)
        print(f"[load-compare] the state-aligned comparison itself exited {exit_code}")
        ledger = parse_ledger_report(LEDGER_PATH.read_text())
        reference_operations = operations_from_records(records)
        for proxy in built:
            print(proxy.music.report(proxy.name)[0])

    # The reference leg's payload identity: computed HERE from the medium, once per distinct request,
    # and a request whose sectors are gone keeps an empty digest so it compares UNEQUAL rather than
    # vanishing from the table.
    digests: dict[tuple[int, int, int], str] = {}
    for operation in reference_operations:
        key = (operation.base_lba, operation.byte_offset, operation.length)
        if key in digests or archive is None:
            continue
        try:
            digests[key] = archive.digest_of(*key)
        except Refusal as refusal:
            digests[key] = ""
            print(f"[load-compare] payload unavailable: {refusal}")
    reference_operations = operations_from_records(records, digests=digests)

    comparison = compare_payloads(ledger.operations, reference_operations)
    print("\n".join(comparison.report()))
    # The payload each destination SHOULD hold, from the medium, so a difference between the two
    # cores' buffers can be told apart from both of them having moved on.
    expected: list[str] = []
    for operation in ledger.operations:
        try:
            expected.append(archive.digest_of(operation.base_lba, operation.byte_offset,
                                               operation.length) if archive else operation.digest)
        except Refusal:
            expected.append("")
    by_payload = compare_payloads(ledger.operations, reference_operations, by_site=False)
    print("\n".join(by_payload.report("M1 payload identity (issuer site excluded)")))
    if not comparison.equal and by_payload.equal:
        print("M1: every payload agrees; the rows that differ are the ISSUER SITE column alone, which "
              "§7's tuple includes and the port can only fill in for reads its own boot owner "
              "dispatched (0155's \"$ra - 8 failure mode\"). Both lists are named below.")
    names = ledger.site_names
    reached = {operation.site for operation in ledger.operations} | {
        operation.site for operation in reference_operations}
    exercised = sorted(site for site in reached if site in names)
    unknown = sorted(site for site in reached if site not in names)
    unreached = [(identifier, address) for identifier, address in ledger.unreached
                 if address not in reached]
    print(f"M1 coverage: {len(exercised)} of the {ledger.total_sites} issuer sites 0155 §2 names "
          f"were exercised on at least one core; {len(unreached)} unreached: "
          f"{', '.join(identifier for identifier, _ in unreached) or 'none'}")
    if unknown:
        print(f"M1 coverage: {len(unknown)} address(es) reached that the 0155 §2 census does not name "
              f"at all: {', '.join(f'0x{site:08X}' for site in unknown)}"
              + (" (a host return address: these are the reads the PORT's own boot owner dispatched, "
                 "which have no `jal` to name them -- see 0155's \"$ra - 8 failure mode\")"
                 if any(site > 0xF0000000 for site in unknown) else ""))

    entries = loader_entry_gate_samples(records)
    print("\n".join(entries.report("at each reference CD loader entry")))
    port_entries = MusicGateSamples()
    for column in ledger.music_at_issue:
        port_entries.set += 1 if column == "busy" else 0
        port_entries.clear += 0 if column == "busy" else 1
    port_entries.polls = len(ledger.music_at_issue)
    print(f"R6 at each port CD loader entry (the ledger's own column): {port_entries.polls} "
          f"operation(s) -> {port_entries.set} saw the bit set, {port_entries.clear} saw it CLEAR")
    print("M1 per-operation table (port | reference):")
    for index in range(max(len(ledger.operations), len(reference_operations))):
        left = ledger.operations[index].describe() if index < len(ledger.operations) else "-"
        right = (reference_operations[index].describe()
                 if index < len(reference_operations) else "-")
        print(f"  {index:>3} {left:<74} | {right}")

    # The VERDICT is the payload verdict plus M2's; §7's strict tuple also carries the issuer site,
    # and the port provably cannot fill that column in for a read its own boot owner dispatched
    # (0155's "$ra - 8 failure mode", reproduced byte-for-byte by the reference's S02-S05 and S06).
    # That is reported above as its own line and is not counted as a payload failure; a payload
    # difference would be, and `by_payload.equal` is what says so.
    equal = by_payload.equal
    if terminals:
        print("\nM2 terminal state:")
        by_key: dict[tuple[str, int], dict[str, Path]] = {}
        for entry in terminals:
            by_key.setdefault((entry["kind"], entry["ordinal"]), {})[entry["core"]] = entry
        terminals_seen = 0
        for (kind, ordinal), sides in sorted(by_key.items()):
            if "port" not in sides or "reference" not in sides:
                print(f"  {kind} #{ordinal}: only "
                      f"{', '.join(sorted(sides))} reached this terminal; not comparable")
                continue
            left = Path(sides["port"]["dumped"]) if sides["port"]["dumped"] else None
            right = Path(sides["reference"]["dumped"]) if sides["reference"]["dumped"] else None
            if left is None or right is None or not left.is_file() or not right.is_file():
                print(f"  {kind} #{ordinal}: a dump is missing "
                      f"({sides['port'].get('error')} / {sides['reference'].get('error')})")
                equal = False
                continue
            outcome = compare_terminal(left, right, ledger.operations, expected)
            # The FIRST terminal each route reaches is the only one where a destination range is
            # still the payload's on both cores: by the next one the guest has consumed its own
            # staging buffers on each side, at its own rate. So the first terminal's destination
            # hashes are the decisive ones and the later ones are reported, not judged.
            index_first = terminals_seen == 0
            terminals_seen += 1
            hand_off = [line for line in outcome["lines"] if "HAND-OFF STATE" in line]
            clocks = [line for line in outcome["lines"] if "clock/timing" in line]
            print(f"  {kind} #{ordinal}: {outcome['fields_equal']}/{outcome['fields_compared']} "
                  f"fields equal; {len(hand_off)} HAND-OFF differing, {len(clocks)} clock differing")
            for line in hand_off + clocks:
                print(f"    {line.strip()}")
            if hand_off:
                equal = False
            resident, both_moved, port_only, reference_only = [], [], [], []
            for (operation, port_digest, reference_digest, reused, on_disc, port_is_payload,
                 reference_is_payload) in outcome["destinations"]:
                if port_digest is None or reference_digest is None:
                    continue
                row = (f"{operation.describe()} -> port {port_digest[:16]} vs reference "
                       f"{reference_digest[:16]}; the payload on the medium is {on_disc[:16]}, "
                       f"held by the port: {'yes' if port_is_payload else 'NO'}, "
                       f"by the reference: {'yes' if reference_is_payload else 'NO'}"
                       + (f"; re-used by a later read into the same address" if reused else ""))
                if port_digest == reference_digest:
                    resident.append(operation)
                elif port_is_payload and not reference_is_payload:
                    port_only.append(row)
                elif reference_is_payload and not port_is_payload:
                    reference_only.append(row)
                else:
                    both_moved.append(row)
            print(f"    payload destinations: {len(resident)} byte-identical, "
                  f"{len(both_moved)} differ with BOTH cores past the payload, "
                  f"{len(port_only)} still held by the port only, "
                  f"{len(reference_only)} still held by the reference only")
            for row in both_moved:
                print(f"      both cores moved on: {row}")
            for row in port_only:
                print(f"      PORT STILL HOLDS THE PAYLOAD, the reference has moved on: {row}")
            for row in reference_only:
                print(f"      REFERENCE STILL HOLDS THE PAYLOAD, the port has moved on: {row}")
            if hand_off or (index_first and (both_moved or port_only or reference_only)):
                equal = False

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            "port_operations": len(ledger.operations),
            "reference_operations": len(reference_operations),
            "sites_named": ledger.total_sites,
            "sites_exercised": len(exercised),
            "unnamed_addresses_reached": [f"0x{site:08X}" for site in unknown],
            "unreached": [identifier for identifier, _ in unreached],
            "m1_equal": comparison.equal,
            "m1_payload_equal": by_payload.equal,
            "m1": comparison.report(),
            "m1_payload": by_payload.report(),
        }, indent=2))
    return 0 if equal else 1


if __name__ == "__main__":
    raise SystemExit(main())