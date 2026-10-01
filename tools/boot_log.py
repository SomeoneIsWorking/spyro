"""boot_log.py — read a boot run's own log into the counters a boot claim needs, with denominators.

The port prints everything a Spyro 2/3 boot measurement needs, but across five differently shaped
lines. This module is the only reader of them, so `boot_run.py` and its selftest cannot disagree
about what a line means. It is pure: text in, a `BootReport` out, nothing launched.

WHAT IT REFUSES TO DO is report a count it did not read. A run that aborts before its `run complete`
line has NO executed-block, instruction, fault or fallback totals, and every one of those comes back
as `None` and prints as NOT MEASURED. Printing 0 there would read as "no faults" about a run that
never finished — the confident zero this repository has been bitten by before.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_PAIRS = re.compile(r"(\w+)=(\d+)")
_RUN_COMPLETE = re.compile(r"\[runtime\] run complete: (?P<body>.*)")
_FALLBACK = re.compile(r"Lightrec fallback telemetry \[run-complete\]: (?P<body>.*)")
_REASONS = re.compile(r"\breasons\{(?P<body>[^}]*)\}")
_REFUSED = re.compile(r"refused_reasons\{(?P<body>[^}]*)\}")
_RESUME = re.compile(r"delivered (?P<fields>\d+) field\(s\) in total .*ending the run at resume (?P<pc>0x[0-9A-Fa-f]+)")
_STOP = re.compile(r"\] Spyro \d stopped: (?P<body>.*)")
_STOP_PC = re.compile(r"at guest pc=(?P<pc>0x[0-9A-Fa-f]+) ra=(?P<ra>0x[0-9A-Fa-f]+)")
_STOP_EXIT = re.compile(r"exit=(?P<exit>.*?) at guest pc=")
_FIELD_BOUND = re.compile(
    r"boot prefix delivered (?P<fields>\d+) field\(s\) in one call without returning and gave up on the guest "
    r"at resume (?P<pc>0x[0-9A-Fa-f]+) \(bound (?P<bound>\d+)\)"
)
_BOOT_RETURNED = re.compile(r"boot prefix returned after (?P<steps>\d+) step\(s\) and (?P<fields>\d+) field\(s\)")
_PUBLISHED = re.compile(
    r"\[cd\] stock CdRead of (?P<sectors>\d+) sector\(s\) from LBA (?P<lba>\d+) published as image "
    r"(?P<image>\d+) generation (?P<generation>\d+)"
)
_CD_READ = re.compile(r"\[cd\] CdRead (?P<sectors>\d+) sector\(s\) .* from LBA (?P<lba>\d+)")
_QUEUED = re.compile(r"\[cdirq\] stock CdRead of \d+ sector\(s\) queued its data-ready completion")
_DELIVERED = re.compile(r"\[cdirq\] CD data-ready -> callback")
_DECLINED = re.compile(r"\[cdirq\] CD data-ready owed, (?:DEFERRED|NOTHING DELIVERED)")


@dataclass
class BootReport:
    # "resume" (named, run complete) | "stop" (named, process aborted) | "cap" (run complete at the
    # product-step cap, which is a run length and not a diagnosis of the guest)
    end_kind: str | None = None
    end_pc: str | None = None
    end_detail: str = ""
    fields: int | None = None
    product_steps: int | None = None
    presentation_fences: int | None = None
    translated_blocks: int | None = None
    executed_blocks: int | None = None
    executed_instructions: int | None = None
    faults: int | None = None
    fallback_blocks: int | None = None
    fallback_instructions: int | None = None
    fallback_reasons: dict[str, int] = field(default_factory=dict)
    refused_reasons: dict[str, int] = field(default_factory=dict)
    reads: list[tuple[int, int]] = field(default_factory=list)  # (sectors, LBA) per CdRead issued
    completions_queued: int = 0
    deliveries: int = 0
    deferrals: int = 0
    boot_returned: tuple[int, int] | None = None  # (steps, fields) the boot prefix took to return
    published: list[tuple[int, int, int]] = field(default_factory=list)  # (LBA, sectors, generation)

    @property
    def end_named(self) -> bool:
        return self.end_kind is not None

    @property
    def cd_matches(self) -> bool:
        """Every read the guest issued was delivered exactly once, and none was deferred forever."""
        return bool(self.reads) and self.deliveries == len(self.reads) and self.completions_queued == len(self.reads)


def _pairs(body: str) -> dict[str, int]:
    return {key: int(value) for key, value in _PAIRS.findall(body)}


def parse(log: str) -> BootReport:
    report = BootReport()
    for line in log.splitlines():
        if (m := _RUN_COMPLETE.search(line)) is not None:
            counts = _pairs(m["body"])
            report.fields = counts.get("fields")
            report.product_steps = counts.get("product_steps")
            report.presentation_fences = counts.get("presentation_fences")
            report.translated_blocks = counts.get("translated_blocks")
            report.executed_blocks = counts.get("executed_blocks")
            report.executed_instructions = counts.get("executed_instructions")
            report.faults = counts.get("faults")
        elif (m := _FALLBACK.search(line)) is not None:
            body = m["body"]
            counts = _pairs(_REASONS.sub("", _REFUSED.sub("", body)))
            report.fallback_blocks = counts.get("fallback_blocks")
            report.fallback_instructions = counts.get("fallback_instructions")
            if (r := _REASONS.search(_REFUSED.sub("", body))) is not None:
                report.fallback_reasons = _pairs(r["body"])
            if (r := _REFUSED.search(body)) is not None:
                report.refused_reasons = _pairs(r["body"])
        elif (m := _RESUME.search(line)) is not None:
            report.end_kind = "resume"
            report.end_pc = m["pc"]
            report.end_detail = "boot step bound reached while polling"
            if report.fields is None:
                report.fields = int(m["fields"])
        elif (m := _FIELD_BOUND.search(line)) is not None:
            report.end_kind = "resume"
            report.end_pc = m["pc"]
            report.end_detail = f"boot field bound {m['bound']} reached inside one call"
        elif (m := _BOOT_RETURNED.search(line)) is not None:
            report.boot_returned = (int(m["steps"]), int(m["fields"]))
        elif (m := _PUBLISHED.search(line)) is not None:
            report.published.append((int(m["lba"]), int(m["sectors"]), int(m["generation"])))
        elif (m := _STOP.search(line)) is not None and report.end_kind != "resume":
            body = m["body"]
            report.end_kind = "stop"
            if (p := _STOP_PC.search(body)) is not None:
                report.end_pc = p["pc"]
            if (e := _STOP_EXIT.search(body)) is not None:
                report.end_detail = e["exit"]
            stop_counts = _pairs(body)
            report.fields = stop_counts.get("fields")
            report.product_steps = stop_counts.get("step")
            report.presentation_fences = stop_counts.get("presents")
        elif (m := _CD_READ.search(line)) is not None:
            report.reads.append((int(m["sectors"]), int(m["lba"])))
        elif _QUEUED.search(line) is not None:
            report.completions_queued += 1
        elif _DELIVERED.search(line) is not None:
            report.deliveries += 1
        elif _DECLINED.search(line) is not None:
            report.deferrals += 1
    if report.end_kind is None and report.product_steps is not None and report.faults is not None:
        # A run that completed its own accounting with no stop and no resume ended at the product-step cap.
        report.end_kind = "cap"
        report.end_detail = "the run reached its product-step cap with no fault and no stop"
    return report


def _value(number: int | None, why: str) -> str:
    return f"{number:,}" if number is not None else f"NOT MEASURED ({why})"


def render(report: BootReport, *, title: str) -> str:
    aborted = "the process aborted before its run-complete line" if report.end_kind == "stop" else "no run-complete line"
    lines = [f"boot run: {title}"]
    if report.end_kind == "resume":
        lines.append(f"  end: NAMED RESUME at {report.end_pc} — {report.end_detail} (run complete)")
    elif report.end_kind == "cap":
        lines.append(f"  end: PRODUCT-STEP CAP — {report.end_detail} (run complete)")
    elif report.end_kind == "stop":
        lines.append(f"  end: NAMED STOP at guest pc {report.end_pc} — {report.end_detail} (process aborted)")
    else:
        lines.append("  end: NOT IDENTIFIED — neither a named resume nor a named stop was found in the log")
    if report.boot_returned is not None:
        steps, fields = report.boot_returned
        lines.append(f"  boot prefix:             returned after {steps:,} steps and {fields:,} fields")
    else:
        lines.append("  boot prefix:             did NOT return")
    lines += [
        f"  fields delivered:        {_value(report.fields, 'no field count in the log')}",
        f"  product steps:           {_value(report.product_steps, aborted)}",
        f"  presentation fences:     {_value(report.presentation_fences, aborted)}",
        f"  translated blocks:       {_value(report.translated_blocks, aborted)}",
        f"  executed blocks:         {_value(report.executed_blocks, aborted)}",
        f"  executed instructions:   {_value(report.executed_instructions, aborted)}",
        f"  faults:                  {_value(report.faults, aborted)}",
        f"  fallback blocks:         {_value(report.fallback_blocks, aborted)}",
        f"  fallback instructions:   {_value(report.fallback_instructions, aborted)}",
    ]
    if report.fallback_reasons:
        reasons = ", ".join(f"{k}={v}" for k, v in report.fallback_reasons.items())
        lines.append(f"  fallback by reason:      {reasons}")
        refused = ", ".join(f"{k}={v}" for k, v in report.refused_reasons.items())
        lines.append(f"  refused by reason:       {refused}")
    reads = len(report.reads)
    if report.published:
        lines.append(
            f"  images published:        {len(report.published)} of {reads} reads "
            f"(LBA, sectors, generation: {', '.join(f'{l}/{n}/g{g}' for l, n, g in report.published)})"
        )
    else:
        lines.append("  images published:        NONE seen — no read was published as a code image")
    lines.append(
        f"  CD: reads issued {reads} ({', '.join(f'{s}x@LBA{l}' for s, l in report.reads) or 'none'}); "
        f"completions queued {report.completions_queued}; cd_ready delivered {report.deliveries}; "
        f"deferred/declined {report.deferrals}"
    )
    if reads == 0:
        lines.append("  CD verdict: NO READS SEEN — the cd channel logged none, so this says nothing about delivery")
    elif report.cd_matches:
        lines.append(f"  CD verdict: MATCH — {report.deliveries} deliveries for {reads} reads, each queued once")
    else:
        lines.append(
            f"  CD verdict: MISMATCH — {report.deliveries} deliveries and {report.completions_queued} completions "
            f"queued for {reads} reads"
        )
    return "\n".join(lines)
