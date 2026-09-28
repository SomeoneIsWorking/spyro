# Ghidra headless post-script: decompile a NAMED target set and record what it did.
#
# Runs under Ghidra's Python provider. Invoked by tools/decomp_ghidra.py, which owns the project,
# the import base, the target set and the admission rules; this script owns only what can be asked
# from INSIDE Ghidra: the functions, their bodies, and the facts about them.
#
#   Script args: <outdir> <addr_hex> [addr_hex ...]
#
# The arguments are FLATTENED ON WHITESPACE before use, and that is not tidiness. Ghidra 12 runs
# this under PyGhidra (`pyghidraRun -H`), and that launcher forwards `-postScript` arguments as ONE
# space-joined string, so a caller that passes three addresses through it receives a single argument
# holding all three. The framework tool `psxport/tools/ghidra_decomp.py` records the same fact for
# the same reason. Without flattening, one invocation form decompiles all targets and the other
# decompiles none and reports one -- and "one" is indistinguishable from a corpus of one.
#
# Flattening is not a substitute for the short-read check. The manifest records the flattened
# `requested` list, and the caller (tools/decomp_manifest.py, rule 1) compares it against the target
# set IT asked for, so a target that never arrived is named as missing rather than read as empty.
#
# WHAT THIS WRITES, into <outdir> (which the caller places under the repository's gitignored
# scratch/ -- the decompiled C is derived from a copyrighted image and must never be committed):
#   manifest.json      the machine-readable record, including what was asked for and what was served
#   <addr>.c           the decompiled body
#   <addr>.asm         Ghidra's own disassembly of the same function, so the caller can check bytes
#
# THE TRUNCATION MITIGATION, kept because the workspace has already been bitten by removing it.
# Ghidra's non-returning-function analyzer guesses from call-site shape, and on a PSX RAM dump it
# mislabels ordinary leaf helpers as noreturn. Every caller then decompiles to a fabricated
# `return 0` with the body after the call discarded -- output that READS LIKE A COMPLETE FUNCTION
# and is not one. `decomp_noreturn_clear` is the switch; the caller always sets it to "all", and the
# manifest records HOW MANY flags were cleared, because a mitigation that cleared nothing is inert
# and the caller refuses an inert one (decomp_manifest rule 5).
#
# THE BODY IS CHECKED, NOT ASSUMED. A target reached only through a jump table has no Function
# object from auto-analysis, so a function is created on demand; and if that fails the manifest says
# WHICH ADDRESS had no function, so a short answer names itself instead of reading as "nothing
# there".

import json
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

flattened = [token for value in getScriptArgs() for token in str(value).split()]
if len(flattened) < 2:
    raise Exception(
        "decomp_postscript: expected <outdir> <addr_hex> [addr_hex...], got %d token(s) after "
        "flattening on whitespace. The count is load-bearing: an invocation form that joined or "
        "dropped the addresses would otherwise decompile one target and report one target, which "
        "reads as a complete answer." % len(flattened))

outdir = flattened[0]
requested = [int(token, 16) for token in flattened[1:]]
os.makedirs(outdir, exist_ok=True)

# ---- the no-return mitigation, and its own count -------------------------------------------------
clear_mode = os.environ.get("decomp_noreturn_clear", "all").strip().lower()
decomp = DecompInterface()
decomp.toggleCCode(True)
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()
fm = currentProgram.getFunctionManager()

cleared = 0
if clear_mode in ("all", ""):
    for fn in fm.getFunctions(True):
        if fn.hasNoReturn():
            fn.setNoReturn(False)
            cleared += 1
elif clear_mode != "none":
    wanted = set(int(token, 16) for token in clear_mode.replace(",", " ").split())
    for fn in fm.getFunctions(True):
        if fn.hasNoReturn() and fn.getEntryPoint().getOffset() in wanted:
            fn.setNoReturn(False)
            cleared += 1

entries = []
for address in sorted(set(requested)):
    name = "%08x" % address
    # NOT toAddr(ea): these are KSEG0 addresses (>= 0x80000000) and the int overload overflows
    # Java's signed int. Go through the address factory with a hex STRING.
    addr = currentProgram.getAddressFactory().getAddress(name)
    fn = fm.getFunctionAt(addr)
    created = False
    if fn is None:
        fn = createFunction(addr, None)
        created = fn is not None
    entry = {
        "address": name,
        "name": fn.getName() if fn is not None else "",
        "status": "ok",
        "created_function": created,
        "instr_count": 0,
        "body_lines": 0,
        "body_chars": 0,
        "noreturn_flag": False,
        "noreturn_warning": False,
        "c_path": name + ".c",
        "asm_path": name + ".asm",
        "note": "",
    }
    if fn is None:
        entry["status"] = "no_function"
        entry["note"] = ("could not create a function here: either this is not code in the "
                         "imported image, or the bytes at it do not form a body Ghidra accepts")
        entries.append(entry)
        continue
    entry["noreturn_flag"] = bool(fn.hasNoReturn())
    body = currentProgram.getListing()
    instructions = list(body.getInstructions(fn.getBody(), True))
    entry["instr_count"] = len(instructions)

    # The disassembly, so the caller can check the tool's own view of the bytes against the image.
    asm_lines = []
    for insn in instructions:
        asm_lines.append("%08x  %s  %s" % (insn.getAddress().getOffset(),
                                            insn.toString().split(" ")[0],
                                            insn.toString()))
    with open(os.path.join(outdir, name + ".asm"), "w") as handle:
        handle.write("\n".join(asm_lines) + "\n")

    result = decomp.decompileFunction(fn, 90, monitor)
    if result is None or not result.decompileCompleted():
        entry["status"] = "decompile_failed"
        entry["note"] = (result.getErrorMessage() if result is not None else "no result object")
        entries.append(entry)
        continue
    text = result.getDecompiledFunction().getC()
    with open(os.path.join(outdir, name + ".c"), "w") as handle:
        handle.write(text)
    entry["body_lines"] = len([row for row in text.splitlines() if row.strip()])
    entry["body_chars"] = len(text)
    entry["noreturn_warning"] = "Subroutine does not return" in text
    entries.append(entry)

manifest = {
    "program": currentProgram.getName(),
    "language": str(currentProgram.getLanguageID()),
    "image_base": "%08x" % currentProgram.getImageBase().getOffset(),
    "requested": ["%08x" % address for address in requested],
    "noreturn_clear_mode": clear_mode,
    "noreturn_cleared": cleared,
    "functions_scanned": fm.getFunctionCount(),
    "entries": entries,
}
with open(os.path.join(outdir, "manifest.json"), "w") as handle:
    handle.write(json.dumps(manifest, indent=1))

print("[decomp-postscript] asked %d, served %d; cleared %d non-return flag(s) over %d function(s)"
      % (len(requested), len(entries), cleared, fm.getFunctionCount()))
