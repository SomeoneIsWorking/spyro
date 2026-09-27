#!/usr/bin/env python3
"""Ask the framework's PSXPORT_STORE_OBSERVE surface, and show whether it can answer at all.

WHY THIS EXISTS. `runtime/cpu/store_observe.{h,cpp}` was added to the framework as the product
surface for the dynarec store observer — the only instrument that names a translated store's GUEST
PC with the full register file — and `PSXPORT_STORE_OBSERVE` is its configuration. The live
divergence hunt in docs/issues/0133 was blocked on exactly that question, so before asking it a
question it has to be shown to ANSWER one. A diagnostic that prints nothing has told us nothing,
and this area has a history of exactly that.

THE NEGATIVE FIRST, because that is the whole point. Before any product run, this establishes, by
measurement on the linked artifact, whether the surface is even REACHABLE:

  * how many source files it scanned, and how many call sites of `store_observe_configure` and
    `store_observe_report` it found in them;
  * whether the two entry points are DEFINED in the framework archive the product links;
  * whether they SURVIVED into the linked product at all;
  * whether the exact format strings the arming line and the per-target report use are present in
    the product's bytes;
  * and, as the control that keeps the negative honest, whether the configuration VARIABLE
    `PSXPORT_STORE_OBSERVE` IS linked — so a run that says "not watched" cannot be explained by the
    knob being unknown.

A surface that is defined-but-unreachable and a surface that is absent are different defects, and
the report says which.

The positive control is the TITLE's own already-wired `HandoffStoreObserver`
(titles/spyro1/core/handoff_store_observer.cpp), which calls the same
`LightrecExecutor::configureStoreObserver` seam directly and reports per-target counts with
`executedJitInstructions` beside them. `--control` runs the product with `PSXPORT_DEBUG=handoff-store`
so the observer is proven live IN THIS BINARY on addresses written every frame, which is what makes
the env-driven surface's silence attributable to wiring rather than to a dead instrument.

    uv run --frozen python tools/probe_store_observe_wiring.py --wiring
    uv run --frozen python tools/probe_store_observe_wiring.py --wiring --binary build/bin/spyro_port
    uv run --frozen python tools/probe_store_observe_wiring.py --control --ticks 400
    uv run --frozen python tools/probe_store_observe_wiring.py --address 0x80078AE0 --ticks 400
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
PSXPORT = ROOT / "external" / "psxport"
OUT = ROOT / "scratch" / "storeobs"

# The two entry points the surface is made of, and the strings only its arming/report can print.
ENTRY_POINTS = ("store_observe_configure", "store_observe_report")
SURFACE_STRINGS = (
    "watching {} guest address(es) for stores",  # store_observe_configure's denominator line
    "refusing PSXPORT_STORE_OBSERVE",  # its refusal line
    "last_guest_pc",  # store_observe_report's per-target row
    "callback_lines",  # and its callback-count column
)
CONFIG_VAR = "PSXPORT_STORE_OBSERVE"
# The control: the title's own observer, which arms the SAME seam. Its targets are written every
# frame (g_GameTick at 0x80033A6C, the resident PadVSync level tick at 0x80053C90).
CONTROL_CHANNEL = "handoff-store"


def _linked_call_sites(binary: Path, names: tuple[str, ...]) -> dict[str, list[int]]:
    """Direct x86-64 call sites of each named function, as VIRTUAL addresses, read from the ELF.

    Three things have to be right or the answer is a confident zero: the symbol's virtual address, the
    instruction's virtual address (which needs the section table, since a rel32 is resolved against the
    address of the FOLLOWING instruction and a file offset is not that address), and the rel32 read
    little-endian and signed. `objdump` supplies the first two, so this does not re-derive them; it is
    used instead of a hand-rolled byte scan precisely so that a mistake here cannot look like a
    measurement."""
    found: dict[str, list[int]] = {name: [] for name in names}
    disasm = subprocess.run(["objdump", "-d", "--demangle", str(binary)], capture_output=True, text=True)
    for line in disasm.stdout.splitlines():
        if "call" not in line or "<" not in line or ">" not in line:
            continue
        try:
            site = int(line.split(":", 1)[0].strip(), 16)
        except ValueError:
            continue
        target = line[line.index("<") + 1:line.index(">")]
        for name in names:
            if target.startswith(name) and site not in found[name]:
                found[name].append(site)
    return {name: sorted(sites) for name, sites in found.items()}


def _defines(names: set[str], name: str) -> bool:
    """Whether a demangled symbol set DEFINES `name`.

    `nm -C` yields the whole demangled signature, so `store_observe_configure` is a PREFIX of
    `store_observe_configure(Core&)`. Comparing the bare name against set members would report
    every one of these as absent, which is the one answer this probe must never get wrong."""
    return any(candidate.startswith(name) for candidate in names)


def _symbols(binary: Path) -> set[str]:
    out = subprocess.run(["nm", "-C", str(binary)], capture_output=True, text=True)
    names: set[str] = set()
    for line in out.stdout.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[1] in "TtWwVv":
            names.add(parts[2].strip())
    return names


def _archive_symbols(archive: Path) -> set[str]:
    if not archive.is_file():
        return set()
    out = subprocess.run(["nm", "-C", str(archive)], capture_output=True, text=True)
    names: set[str] = set()
    for line in out.stdout.splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[1] == "T":
            names.add(parts[2].strip())
    return names


def _strings(binary: Path) -> str:
    out = subprocess.run(["strings", "-a", str(binary)], capture_output=True, text=True)
    return out.stdout


def _call_sites(name: str) -> list[str]:
    """Every line in the framework and this title that names `name`, with the file it is in.

    A definition and a header declaration are counted separately from a call, because the defect
    this probe exists to catch is precisely 'defined, declared, never called'."""
    roots = [PSXPORT / "runtime", PSXPORT / "common", PSXPORT / "tools", PSXPORT / "tests",
             ROOT / "game", ROOT / "titles"]
    hits: list[str] = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".c", ".hpp") or not path.is_file():
                continue
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            for number, line in enumerate(text.splitlines(), start=1):
                if name in line:
                    try:
                        where = path.relative_to(ROOT)
                    except ValueError:
                        where = path
                    hits.append(f"{where}:{number}: {line.strip()[:150]}")
    return hits


def report_wiring(binary: Path) -> int:
    """Print the reachability measurement, then say reachable or not."""
    if not binary.is_file():
        print(f"binary              : {binary} — DOES NOT EXIST, so nothing below was measured")
        return 2
    try:
        shown = binary.resolve().relative_to(ROOT)
    except ValueError:
        shown = binary
    print(f"binary              : {shown} ({binary.stat().st_size} bytes)")
    # Every libpsxport.a this repo can see, so a stale framework-side archive cannot be mistaken
    # for the one the product links and report the surface as unbuilt.
    archives = [p for p in (ROOT / "build" / "libpsxport.a", PSXPORT / "build" / "libpsxport.a")
                if p.is_file()]
    if not archives:
        print("framework archive   : NONE FOUND — the linkage rows below are from the product alone")
    linked = _symbols(binary)
    strings = _strings(binary)

    print()

    print("CALL SITES (the denominator: how much source was scanned, and what it names)")
    total = 0
    callers: dict[str, list[str]] = {}
    for name in ENTRY_POINTS:
        hits = _call_sites(name)
        total += len(hits)
        # A mention is a CALL only when the name is followed by `(` and the rest of the line is not a
        # comment, a declaration, or a definition. Definitions (`name(` at the start of the line),
        # header declarations (ending `;`) and prose mentions (preceded by a backtick) are all
        # `name(`-shaped, and counting them as callers is how a surface gets declared unreachable
        # while the listing right above it names the call.
        callers[name] = [hit for hit in hits
                         if f"{name}(" in hit
                         and not hit.split(": ", 1)[1].lstrip().startswith(("//", f"`{name}(", "*",
                                                                            "void ", "extern "))]
        print(f"  {name}: {len(hits)} mention(s) in runtime/ common/ tools/ tests/ game/ titles/ "
              f"-> {len(callers[name])} call site(s)")
        for hit in hits:
            print(f"      {hit}")
    print(f"  scanned roots      : framework runtime/ common/ tools/ tests/ + this repo game/ titles/ "
          f"-> {total} mention(s) of {len(ENTRY_POINTS)} entry point(s) in total")
    print()

    print("LINKAGE (defined in the framework archive, then survived into the product?)")
    per_archive = {archive: _archive_symbols(archive) for archive in archives}
    for archive, names in per_archive.items():
        try:
            where = archive.resolve().relative_to(ROOT)
        except ValueError:
            where = archive
        print(f"  archive {where}:")
        for name in ENTRY_POINTS:
            print(f"      {name}: {'defined' if _defines(names, name) else 'ABSENT'}")
    for name in ENTRY_POINTS:
        print(f"  product {name}: {'present' if _defines(linked, name) else 'ABSENT'}")
    print()

    print("PRODUCT BYTES (can this surface's own output exist in the product at all?)")
    for text in SURFACE_STRINGS:
        print(f"  {'PRESENT' if text in strings else 'ABSENT ':8} : {text!r}")
    print()

    print("THE KNOB (the control that keeps the negative honest)")
    print(f"  {'PRESENT' if CONFIG_VAR in strings else 'ABSENT ':8} : {CONFIG_VAR} "
          f"(a config var that IS linked but has no reader does nothing at all)")
    print()

    print("REACHABILITY IN THE LINKED BINARY (symbol PRESENCE is not a call)")
    # This is the measurement the verdict is built on, and it is deliberately not `nm`. A symbol being
    # present says the object was linked; it says nothing about whether anything calls it. An earlier
    # version of this probe printed "no CALLER, so the linker never pulled store_observe.cpp.o into
    # the product" on the SAME run that printed "product store_observe_configure: present": two
    # contradictory rows, and the verdict believed the wrong one. The disassembly settles it. An
    # x86-64 direct call is `E8 <rel32>`, so counting the calls whose target is each symbol answers
    # the question instead of inferring it, and `objdump -d` does the section mapping -- because
    # resolving a rel32 against a raw FILE offset instead of the instruction's VIRTUAL address finds
    # zero call sites in ANY binary, which is how this check lies convincingly.
    call_sites = _linked_call_sites(binary, ENTRY_POINTS)
    for name in ENTRY_POINTS:
        sites = call_sites.get(name, [])
        print(f"  {name}: {len(sites)} direct call site(s) in the product: "
              + (", ".join(f"0x{site:08X}" for site in sites[:4]) if sites else "NONE"))
    print()

    print("PRODUCT BYTES (can this surface's own output exist in the product at all?)")
    for text in SURFACE_STRINGS:
        print(f"  {'PRESENT' if text in strings else 'ABSENT ':8} : {text!r}")
    print()

    print("THE KNOB (the control that keeps the negative honest)")
    print(f"  {'PRESENT' if CONFIG_VAR in strings else 'ABSENT ':8} : {CONFIG_VAR} "
          f"(a config var that is linked but has no reader does nothing at all)")
    print()

    linked_in = all(_defines(linked, name) for name in ENTRY_POINTS)
    called_in = all(call_sites.get(name) for name in ENTRY_POINTS)
    if linked_in and called_in:
        print("verdict: REACHABLE - both entry points are linked into this product AND the product's "
              "disassembly contains a direct call to each, so a run may arm it. Symbol presence was "
              "never the test; the call-site rows above are")
        return 0
    built = any(all(_defines(names, name) for name in ENTRY_POINTS) for names in per_archive.values())
    if built and not linked_in:
        missing = ", ".join(name for name in ENTRY_POINTS if not _defines(linked, name))
        print(f"verdict: DEFINED BUT NOT LINKED - the framework COMPILES both entry points (they are "
              f"in the archive above) but {missing} did not survive into this product, so setting "
              f"{CONFIG_VAR} in a product run arms nothing and reports nothing. Any conclusion drawn "
              "from that silence would be wrong.")
    elif built:
        missing = ", ".join(name for name in ENTRY_POINTS if not call_sites.get(name))
        print("verdict: DEFINED AND LINKED BUT NEVER CALLED - the product carries both entry points "
              f"but its disassembly has no direct call to {missing}, so setting {CONFIG_VAR} arms "
              "nothing at run time. A symbol table cannot tell this apart from REACHABLE, which is "
              "why the call-site rows above exist")
    else:
        print("verdict: ABSENT — no inspected archive defines these entry points at all, so the "
              "framework does not build this surface in the state under test")
    return 1


def _environment(log: Path, port: int, extra: dict[str, str]) -> dict[str, str]:
    env = dict(os.environ)
    env.update({
        "PSXPORT_VK_HEADLESS": "1",
        "PSXPORT_NOAUDIO": "1",
        "PSXPORT_NOPACE": "1",
        "PSXPORT_WATCHDOG": "0",
        "PSXPORT_SETTINGS": str(ROOT / "tools" / "shipping_settings.ini"),
        "PSXPORT_ASSET_DIR": str(PSXPORT),
        "PSXPORT_CARD": str(OUT / "card.mcr"),
        "PSXPORT_LOG_FILE": str(log),
    })
    if port:
        env["PSXPORT_DEBUG_SERVER"] = str(port)
    env.update(extra)
    return env


def _ask(port: int, command: str, timeout: float = 20.0) -> str:
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        chunks: list[bytes] = []
        try:
            while True:
                piece = sock.recv(65536)
                if not piece:
                    break
                chunks.append(piece)
                if b"\n" in piece:
                    break
        except socket.timeout:
            pass
    return b"".join(chunks).decode(errors="replace")


def run_control(binary: Path, frames: int, port: int) -> int:
    """POSITIVE CONTROL: prove the observer seam fires in THIS binary, on a per-frame-written word.

    The title's own HandoffStoreObserver calls the same configureStoreObserver the env-driven
    surface calls, and reports per-target counts with the executor's instruction counters. Non-zero
    per-target counts here mean the instrument is live; a silent env-driven surface afterwards is
    then attributable to its missing wiring, not to a dead observer."""
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "control.log"
    env = _environment(log, 0, {"PSXPORT_DEBUG": CONTROL_CHANNEL,
                                "PSXPORT_NATIVE_FRAMES": str(frames)})
    print(f"control: {frames} headless frames, PSXPORT_DEBUG={CONTROL_CHANNEL}")
    result = subprocess.run([str(binary), str(ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28")],
                            env=env, capture_output=True, text=True, timeout=900)
    print(f"exit={result.returncode}")
    text = log.read_text(errors="replace") if log.is_file() else ""
    rows = [line for line in text.splitlines() if "handoff-store" in line]
    if not rows:
        print("verdict: FAIL — the control channel emitted no line, so the observer was not proven live")
        return 1
    nonzero = 0
    for line in rows:
        print(f"  {line.strip()[:220]}")
        # The title's report prints `target=0x... before=N after=N scanned_jit_instructions=N`, and
        # the per-target counts are what say the observer FIRED rather than merely ran. A run that
        # executed guest code and matched nothing would print zeros here, so the test is on the
        # counts and not on the presence of the line.
        match = re.search(r"before=(\d+)\s+after=(\d+)", line)
        if match and (int(match.group(1)) or int(match.group(2))):
            nonzero += 1
    summary = next((line for line in rows if "targets=" in line), "")
    print()
    if nonzero:
        print(f"verdict: CONTROL PASS — {nonzero} per-target row(s) with a NON-ZERO store count, so "
              f"the dynarec store observer really fired in THIS binary on a word written every "
              f"frame.")
        print(f"  {summary.strip()[:220]}")
        print("  That makes the env-driven surface's silence attributable to its missing wiring, "
              "not to a dead observer.")
        return 0
    print("verdict: CONTROL FAIL — the observer ran but every per-target count is ZERO, so nothing "
          "is proven. Summary line:")
    print(f"  {summary.strip()[:220]}")
    return 1


def run_address(binary: Path, address: str, frames: int, port: int) -> int:
    """Ask the env-driven surface for real, and report what it printed — which may be nothing."""
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "observe.log"
    env = _environment(log, 0, {"PSXPORT_STORE_OBSERVE": address,
                                "PSXPORT_DEBUG": "store-observe",
                                "PSXPORT_NATIVE_FRAMES": str(frames)})
    print(f"asking: {frames} headless frames, PSXPORT_STORE_OBSERVE={address}, "
          f"PSXPORT_DEBUG=store-observe")
    result = subprocess.run([str(binary), str(ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28")],
                            env=env, capture_output=True, text=True, timeout=900)
    print(f"exit={result.returncode}")
    text = log.read_text(errors="replace") if log.is_file() else ""
    lines = [line for line in text.splitlines() if "store-observe" in line]
    print(f"log lines mentioning 'store-observe': {len(lines)}")
    for line in lines:
        print(f"  {line.strip()[:220]}")
    armed = [line for line in lines if "watching" in line]
    if not armed:
        print()
        print("verdict: THE SURFACE PRINTED NOTHING — the arming line is absent, so this run watched "
              "nothing. That is NOT evidence about the store; it is evidence the surface is not "
              "called. See --wiring.")
        return 1
    return 0


def run_endpoint(binary: Path, port: int, frames: int) -> int:
    """Verify the live endpoint answers `guest` ON THIS BINARY, then quit it.

    A stale binary answers `? guest (try 'help')` while its own help lists `guest`, which looks
    exactly like a zero measurement. So the check is the ANSWER, not the help text."""
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "endpoint.log"
    env = _environment(log, port, {})
    print(f"endpoint: launching on 127.0.0.1:{port} (no frame cap — the debug server lifts it)")
    process = subprocess.Popen(
        [str(binary), str(ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28")],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        deadline = time.time() + 240.0
        connected = False
        while time.time() < deadline:
            if process.poll() is not None:
                print(f"verdict: FAIL — the product exited (rc={process.returncode}) before binding")
                return 1
            try:
                socket.create_connection(("127.0.0.1", port), timeout=1.0).close()
                connected = True
                break
            except OSError:
                time.sleep(1.0)
        if not connected:
            print("verdict: FAIL — the endpoint never bound; nothing below was measured")
            return 1
        help_text = _ask(port, "help")
        guest = _ask(port, "guest")
        print(f"  help lists guest   : {'yes' if re.search(r'(?m)^\\s*guest\\b', help_text) else 'NO'}")
        print(f"  guest reply        : {guest.strip()[:400] or '(EMPTY)'}")
        answered = "guest:" in guest
        if not answered:
            print()
            print("verdict: FAIL — `guest` did not answer. Either the binary is stale or the "
                  "endpoint is not serving; either way the executor's denominators are unavailable.")
            return 1
        print()
        print("verdict: ENDPOINT PASS — this binary answers `guest` with the executor's own counters")
        return 0
    finally:
        if process.poll() is None:
            try:
                _ask(port, "quit", timeout=5.0)
            except OSError:
                pass
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(process.pid), signal.SIGTERM)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    os.killpg(os.getpgid(process.pid), signal.SIGKILL)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wiring", action="store_true",
                        help="measure whether the surface is reachable in the linked product")
    parser.add_argument("--control", action="store_true",
                        help="positive control: prove the observer seam fires in this binary")
    parser.add_argument("--address", default="",
                        help="ask PSXPORT_STORE_OBSERVE about this guest address")
    parser.add_argument("--endpoint", action="store_true",
                        help="verify the live endpoint answers `guest` on this binary")
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "bin" / "spyro_port")
    parser.add_argument("--ticks", type=int, default=400, help="headless frames for a product run")
    parser.add_argument("--port", type=int, default=5981)
    args = parser.parse_args()

    if not any((args.wiring, args.control, args.address, args.endpoint)):
        parser.error("choose at least one of --wiring --control --address --endpoint")
    result = 0
    if args.wiring:
        result |= report_wiring(args.binary)
    if args.control:
        result |= run_control(args.binary, args.ticks, args.port)
    if args.address:
        result |= run_address(args.binary, args.address, args.ticks, args.port)
    if args.endpoint:
        result |= run_endpoint(args.binary, args.port, args.ticks)
    return 0 if result == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
