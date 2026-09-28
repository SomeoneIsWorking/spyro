#!/usr/bin/env python3
"""Is a Ghidra decompile ADMISSIBLE as evidence? The rules, and what each one refuses.

ONE CONCEPT: the difference between "Ghidra produced C for this function" and "this C is the function".
The two are not the same, and on a PSX RAM dump the difference is the workspace's signature failure
mode: an instrument producing a confident wrong answer rather than an absence.

THE FAILURE THIS EXISTS FOR. Ghidra's non-returning-function analyzer guesses from call-site shape,
and on this image it mislabels ordinary leaf helpers (trig/math tables reached via a jump) as
noreturn. Every CALLER then decompiles to `/* WARNING: Subroutine does not return */` with the whole
body after the call discarded and a fabricated `return 0`. That output reads like a complete function
and is not one, which is worse than no output: it will be trusted. So the post-script clears the flag
(`PSXPORT_CLEAR_NORETURN=all`, the framework tool's own mitigation, kept), and THIS module refuses any
result where the mitigation is inert or the body is short of the evidence the caller asked for.

THE CHECKS, and the failure each one exists for:

  1. served == asked. A manifest that answered 2 of 3 targets is a SHORT READ, and the natural reading
     of the absent tail is "nothing there" -- the exact shape of a manufactured result. Refused, with
     both numbers.
  2. status ok. A target Ghidra could not decompile, or had no function for, is named, not dropped.
  3. the noreturn warning is absent from the BODY TEXT. Checked on the emitted C, not on the
     post-script's own boolean, because the boolean is the thing under test.
  4. the noreturn flag is clear on the function. A function that still carries the flag would
     truncate its CALLERS even though its own body is fine.
  5. the clearing did something. `noreturn_cleared == 0` means the mitigation is inert, so a
     truncation this run cannot see is still a truncation. Refused.
  6. every anchor the caller demanded appears in the body. This is the depth check: anchors are
     chosen from ground truth DEEP in a function, so a body truncated anywhere before them is
     refused rather than filed. Matching is on the hex DIGITS of the value, case-insensitively and
     without requiring an `0x` prefix, because the textual FORM of a constant is a decompiler choice
     and the VALUE is a fact about the image.
  7. the body is not a stub. A three-line body with a `return 0` and nothing else is what a
     truncated decompile looks like, so a floor on the emitted text keeps it out of the evidence.

Nothing here knows what the decompiled C MEANS. It decides admissibility, and it says which rule
each refusal came from, so a refusal is actionable rather than a shrug.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

# Ghidra's own marker for a call it believes does not return. Lower-cased before the search.
NORETURN_WARNING = "subroutine does not return"
# A body at or below this many non-blank lines is a stub. The four animation channels of
# 0x800258F0 alone decompile to hundreds of lines, and the smallest real function measured in this
# image is far longer than four; a body under the floor is a truncation or an empty shell, and both
# are refusals.
MIN_BODY_LINES = 4
MIN_BODY_CHARS = 60


class ManifestRefusal(Exception):
    """The manifest is not admissible. The message names which rule refused and for which target."""


@dataclass
class Entry:
    """One target's decompile, as the post-script recorded it."""

    address: int
    name: str
    status: str
    instr_count: int
    body_lines: int
    body_chars: int
    noreturn_flag: bool
    noreturn_warning: bool
    created_function: bool
    c_path: str
    note: str = ""


@dataclass
class Verdict:
    """The admission decision, per target, with the refusal reasons kept."""

    admitted: list[str] = field(default_factory=list)
    refused: dict[str, str] = field(default_factory=dict)
    asked: int = 0
    served: int = 0
    noreturn_cleared: int = 0
    functions_scanned: int = 0

    @property
    def ok(self) -> bool:
        return not self.refused

    def summary(self) -> str:
        return (f"asked {self.asked}, served {self.served}, admitted {len(self.admitted)}, "
                f"refused {len(self.refused)}; noreturn flags cleared on {self.noreturn_cleared} "
                f"function(s) of {self.functions_scanned} scanned")


def _hex_digits_present(body: str, value: int) -> bool:
    """Is this hex VALUE in the body, in any of the textual forms a decompiler chooses?

    Matched on digits only (`80078560` matches `0x80078560`, `DAT_80078560`, `(undefined4)0x80078560`),
    lower-cased, and NOT anchored to a word boundary -- a `0x180078560`-style literal would match a
    `80078560` anchor by accident, so the caller is expected to choose anchors that cannot be a
    substring of a neighbouring value, and this function says so in its own docstring rather than
    pretending to a guarantee it does not have.
    """
    digits = f"{value:x}"
    return digits in body.lower().replace("0x", "")


def _read_body(root: Path, entry: Entry) -> str:
    path = root / entry.c_path
    if not path.is_file():
        raise ManifestRefusal(
            f"0x{entry.address:08X}: the manifest names {entry.c_path} and that file is not there. "
            "A body that was not written is not evidence, and reporting it as a decompile would be "
            "the same confident wrong answer this module exists to refuse.")
    return path.read_text(errors="replace")


def load(manifest_path: Path) -> tuple[list[Entry], dict]:
    """Parse the post-script's manifest. Refuses a manifest it cannot read or cannot understand."""
    if not manifest_path.is_file():
        raise ManifestRefusal(
            f"no manifest at {manifest_path}. The post-script did not complete, or wrote somewhere "
            "else; that is an ABSENCE and must not be read as 'nothing decompiled'.")
    try:
        document = json.loads(manifest_path.read_text())
    except json.JSONDecodeError as error:
        raise ManifestRefusal(f"{manifest_path} is not JSON: {error}") from error
    for key in ("requested", "entries"):
        if key not in document:
            raise ManifestRefusal(f"{manifest_path} has no '{key}' -- a manifest that does not say "
                                  "what it was asked for cannot be checked for a short answer")
    entries = []
    for raw in document["entries"]:
        try:
            entries.append(Entry(
                address=int(str(raw["address"]), 16),
                name=str(raw.get("name", "")),
                status=str(raw.get("status", "?")),
                instr_count=int(raw.get("instr_count", 0)),
                body_lines=int(raw.get("body_lines", 0)),
                body_chars=int(raw.get("body_chars", 0)),
                noreturn_flag=bool(raw.get("noreturn_flag", False)),
                noreturn_warning=bool(raw.get("noreturn_warning", False)),
                created_function=bool(raw.get("created_function", False)),
                c_path=str(raw.get("c_path", "")),
                note=str(raw.get("note", "")),
            ))
        except (KeyError, TypeError, ValueError) as error:
            raise ManifestRefusal(f"manifest entry {raw!r} is malformed: {error}") from error
    return entries, document


def admit(entries: list[Entry], document: dict, root: Path, targets) -> Verdict:
    """Decide admissibility. `targets` is an iterable of Target (decomp_targets.Target).

    Every refusal is returned in `Verdict.refused` keyed by address, with the rule that refused it.
    Raising is reserved for a manifest that cannot be read at all.
    """
    verdict = Verdict()
    requested = [int(str(value), 16) for value in document["requested"]]
    verdict.asked = len(requested)
    verdict.served = len(entries)
    verdict.noreturn_cleared = int(document.get("noreturn_cleared", 0))
    verdict.functions_scanned = int(document.get("functions_scanned", 0))

    by_address: dict[int, Entry] = {}
    for entry in entries:
        if entry.address in by_address:
            verdict.refused[f"0x{entry.address:08X}"] = (
                f"rule 1 (served == asked): the manifest carries {verdict.served} entries for "
                f"{verdict.asked} requested address(es) and 0x{entry.address:08X} appears more than "
                "once, so which body belongs to it is undetermined")
            continue
        by_address[entry.address] = entry

    # Rule 1: the short read. Named with both numbers, never padded with zeros.
    missing = [address for address in requested if address not in by_address]
    if missing or len(entries) != len(requested):
        verdict.refused["SHORT READ"] = (
            f"rule 1 (served == asked): asked for {verdict.asked} address(es), the manifest served "
            f"{verdict.served}, and {len(missing)} requested address(es) have no entry at all: "
            + ", ".join(f"0x{address:08X}" for address in missing)
            + ". The absent ones were NOT fetched and are not 'empty'.")

    # Rule 5: an inert mitigation. This is a refusal about the RUN, not about one target, and it is
    # why a manifest can come back refused with every individual target clean.
    if verdict.noreturn_cleared <= 0:
        verdict.refused["NORETURN GUARD INERT"] = (
            "rule 5 (the clearing did something): the post-script cleared 0 non-return flags over "
            f"{verdict.functions_scanned} scanned function(s). With the guard inert, a truncation "
            "this run could not see is still a truncation, so no body below is admissible.")

    for target in targets:
        address = target.address
        key = f"0x{address:08X}"
        entry = by_address.get(address)
        if entry is None:
            continue  # already named by rule 1
        if entry.status != "ok":
            verdict.refused[key] = (f"rule 2 (status ok): {entry.status}"
                                    + (f" -- {entry.note}" if entry.note else ""))
            continue
        if entry.noreturn_flag:
            verdict.refused[key] = (f"rule 4 (the flag is clear): 0x{address:08X} still carries "
                                    "Ghidra's non-return flag, so its CALLERS truncate even though "
                                    "its own body is present")
            continue
        try:
            body = _read_body(root, entry)
        except ManifestRefusal as error:
            verdict.refused[key] = f"rule 3 (the body text exists): {error}"
            continue
        lowered = body.lower()
        if NORETURN_WARNING in lowered or entry.noreturn_warning:
            verdict.refused[key] = (
                f"rule 3 (no truncation warning): the body of 0x{address:08X} carries Ghidra's "
                f"'{NORETURN_WARNING}' marker, which means the decompiler DISCARDED the body after "
                "the call it believed did not return. This is the truncation that reads like a "
                "complete function.")
            continue
        lines = [row for row in body.splitlines() if row.strip()]
        if len(lines) < MIN_BODY_LINES or len(body) < MIN_BODY_CHARS:
            verdict.refused[key] = (
                f"rule 7 (not a stub): the body of 0x{address:08X} is {len(lines)} non-blank line(s) "
                f"and {len(body)} character(s), under the {MIN_BODY_LINES}/{MIN_BODY_CHARS} floor. A "
                "body this short is a truncation or an empty shell, and either way it is not the "
                "function.")
            continue
        absent = [value for value in target.anchors
                  if not _hex_digits_present(body, value)]
        if absent:
            verdict.refused[key] = (
                f"rule 6 (the demanded evidence is present): the body of 0x{address:08X} is missing "
                + ", ".join(f"0x{value:08X}" for value in absent)
                + ". Those values are recorded as being inside this function, so their absence "
                  "means the body stops short of them.")
            continue
        if key in verdict.refused:
            continue
        verdict.admitted.append(key)
    return verdict


# ---------------------------------------------------------------------------------------------
# The selftest. Every case is a REFUSAL that must go red, and the last case is the falsifier for the
# whole module: the positive fixture with its deep anchor REMOVED must be refused, which is what
# proves rule 6 can fail. A validator that cannot fail is the failure being guarded against.
# ---------------------------------------------------------------------------------------------

GOOD_BODY = """void FUN_800258f0(void)

{
    undefined4 uVar1;
    long lVar2;
    g_EnvironmentAnimations = DAT_80078560;
    uVar1 = 0x1100984a;
    lVar2 = 0x1000784a;
    if (uVar1 != 0) {
        FUN_8001000c(0, 0, 0);
    }
    return;
}
"""

# The truncation shape the guard exists for: the body stops right after the call Ghidra believed
# did not return, fabricates a value, and looks finished.
TRUNCATED_BODY = """void FUN_800258f0(void)

{
    DAT_80078560 = 0x1100984a;
    FUN_8000a000(); /* WARNING: Subroutine does not return */
    return 0;
}
"""
def _fixture(root: Path, bodies: dict[str, str], document: dict,
             mutate=None) -> tuple[Path, Path]:
    """Write one self-contained manifest plus its C bodies under `root`, and return both paths.

    `mutate` receives the document dict after the entries are built, so a case can set the one
    field it is about (a duplicate entry, a failed status) without every case carrying the machinery.
    """
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.c"):
        stale.unlink()
    entries = []
    for address, text in bodies.items():
        name = f"{int(address, 16):08x}.c"
        (root / name).write_text(text)
        entries.append({
            "address": address,
            "name": "FUN_" + address[2:],
            "status": "ok",
            "instr_count": 1234,
            "body_lines": len([row for row in text.splitlines() if row.strip()]),
            "body_chars": len(text),
            "noreturn_flag": False,
            "noreturn_warning": False,
            "created_function": False,
            "c_path": name,
        })
    document = dict(document)
    document["entries"] = entries
    if mutate is not None:
        mutate(document)
    path = root / "manifest.json"
    path.write_text(json.dumps(document, indent=1))
    return path, root


BASE_DOCUMENT = {"requested": ["800258f0"], "noreturn_cleared": 41, "functions_scanned": 4000}
CONTROL_ADDRESS = 0x800258F0
# The ground truth the fixture stands in for: recorded out of the image for the S_World renderer by
# docs/issues/0134, the animation global and both GTE command words.
CONTROL_ANCHORS = (0x80078560, 0x1100984A, 0x1000784A)


def _duplicate(document: dict) -> None:
    document["entries"].append(dict(document["entries"][0]))


def _failed(document: dict) -> None:
    document["entries"][0]["status"] = "decompile_failed"
    document["entries"][0]["note"] = "Decompiler timeout after 90 s"


def selftest(scratch: Path) -> int:
    """The positive, six refusals, and the falsifier for rule 6 itself.

    Every case is written to `scratch/decomp/selftest/`, never to /tmp.
    """
    from decomp_targets import Target  # the real Target, not a stand-in

    root = scratch / "decomp" / "selftest"
    targets = [Target(CONTROL_ADDRESS, "fixture control", "control", CONTROL_ANCHORS,
                      "self-test fixture")]
    cases = (
        ("positive: a complete body carrying every demanded value is ADMITTED",
         {"800258f0": GOOD_BODY}, BASE_DOCUMENT, None, "admit"),
        ("truncation: a body carrying the noreturn marker is REFUSED",
         {"800258f0": TRUNCATED_BODY}, BASE_DOCUMENT, None, "refuse:rule 3"),
        ("short read: 1 entry for 2 requested addresses is REFUSED, naming both numbers",
         {"800258f0": GOOD_BODY}, {**BASE_DOCUMENT, "requested": ["800258f0", "8004eba8"]},
         None, "refuse:SHORT READ"),
        ("a target Ghidra could not decompile is NAMED, not dropped",
         {"800258f0": GOOD_BODY}, BASE_DOCUMENT, _failed, "refuse:rule 2"),
        ("an inert clearing is a RUN-level refusal even when every body is clean",
         {"800258f0": GOOD_BODY}, {**BASE_DOCUMENT, "noreturn_cleared": 0}, None,
         "refuse:NORETURN GUARD INERT"),
        ("a duplicate entry for one address is REFUSED",
         {"800258f0": GOOD_BODY}, BASE_DOCUMENT, _duplicate, "refuse:rule 1"),
        ("a function that still carries the noreturn flag is REFUSED",
         {"800258f0": GOOD_BODY}, BASE_DOCUMENT,
         lambda d: d["entries"][0].update(noreturn_flag=True), "refuse:rule 4"),
        ("a stub body is REFUSED (rule 7)",
         {"800258f0": "void FUN_800258f0(void)\n{\n    return;\n}\n"}, BASE_DOCUMENT, None,
         "refuse:rule 7"),
    )
    failures = 0
    for name, bodies, document, mutate, expectation in cases:
        path, fixture_root = _fixture(root, bodies, document, mutate)
        entries, loaded = load(path)
        verdict = admit(entries, loaded, fixture_root, targets)
        if expectation == "admit":
            good = verdict.ok
        else:
            # The needle is looked for in the refusal KEYS as well as the reasons: two rules name
            # themselves in the key (a short read, an inert guard) and the rest in the reason text.
            # Matching only the reasons would leave those two cases unable to fail.
            needle = expectation.split(":", 1)[1]
            good = (any(needle == key for key in verdict.refused)
                    or any(needle in reason for reason in verdict.refused.values()))
        if good:
            print(f"[decomp-manifest] selftest {name} -> OK")
        else:
            failures += 1
            print(f"[decomp-manifest] selftest FAIL {name} -> {verdict.summary()} refusals="
                  + json.dumps({k: v.splitlines()[0] for k, v in verdict.refused.items()}))

    # THE FALSIFIER for rule 6: the SAME positive body with one demanded value gone must go red.
    # Without this case rule 6 could be a no-op that always agrees, which is the failure this module
    # was written to refuse.
    stripped = GOOD_BODY.replace("0x1000784a", "0x0")
    path, fixture_root = _fixture(root, {"800258f0": stripped}, BASE_DOCUMENT)
    entries, loaded = load(path)
    verdict = admit(entries, loaded, fixture_root, targets)
    if verdict.ok or not any("rule 6" in reason for reason in verdict.refused.values()):
        failures += 1
        print("[decomp-manifest] selftest FAIL a body missing one demanded value was STILL ADMITTED, "
              f"so rule 6 cannot fail. {verdict.summary()}")
    else:
        print("[decomp-manifest] selftest a body missing one demanded value is REFUSED by rule 6 "
              "-> OK (the check can fail)")

    # A missing manifest is an ABSENCE and must refuse rather than read as "nothing decompiled".
    try:
        load(root / "absent-manifest.json")
        failures += 1
        print("[decomp-manifest] selftest FAIL a missing manifest was read as a valid empty one")
    except ManifestRefusal:
        print("[decomp-manifest] selftest a missing manifest is REFUSED, not read as empty -> OK")

    print(f"[decomp-manifest] selftest {'FAILED' if failures else 'PASS'}: {failures} failure(s)")
    return 1 if failures else 0
