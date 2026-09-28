# Ghidra headless pre-script: turn OFF the analyzers that GUESS, before auto-analysis runs.
#
# WHY THIS EXISTS, and it is the same lesson as the non-return clearing in the post-script.
#
# Ghidra's auto-analysis is built for images with symbols, sections and a compiler's calling
# convention. A PSX executable imported as ONE raw binary at a flat base has none of those, and
# several analyzers respond by inventing structure. Measured on `SCUS_942.28` at 0x800258F0,
# 2026-09-28:
#
#   * the decompiler emitted 3,030 lines covering everything EXCEPT the environment-animation phase
#     at 0x800259FC-0x800261A0, and said so only in a header line nobody reads:
#         WARNING: Instruction at (ram,0x80025b74) overlaps instruction at (ram,0x80025b70)
#     0x80025b70 is a branch and 0x80025b74 its delay slot. An analyzer decided 0x80025b74 was also
#     the start of a different instruction, the region stopped being one flow, and the decompiler
#     dropped it -- producing output that reads like a complete function and is missing 6% of it.
#     The three values that prove the region is real (g_EnvironmentAnimations 0x80078560, the GTE
#     INTPL word 0x1100984A, the DPCS word 0x1000784A, all read out of the image by
#     tools/probe_guest_disasm.py) are absent from the decompiled C, and the post-script's anchor
#     check refused the body. THAT is the guard working, and this file is the fix it asked for.
#
#   * the function inventory the decompile depends on is produced by analyzers that place function
#     boundaries from call-site shape, so on this image they are a source of wrong answers, not of
#     coverage.
#
# WHAT REPLACES THEM is not a guess of this script's own: the post-script creates each named target's
# function on demand from its entry address, and every body is then checked against values read out
# of the image by tools/probe_guest_disasm.py.
#
# STATUS, AND IT IS NOT A FIX YET. Measured 2026-09-28: all four disables FAILED on this Ghidra.
# `GhidraScript.setAnalysisOption` raises "No matching overloads found" for both the (str, bool) and
# the (str, str) forms under PyGhidra -- the Python str is not being mapped to java.lang.String -- and
# `from ghidra.app.analyze.AutoAnalysisManager import AutoAnalysisManager` fails to import, so the
# option names could not be enumerated either. The run therefore proceeded with auto-analysis
# UNCHANGED, and the control 0x800258F0 was still refused for missing the animation phase.
#
# So this file currently DISABLES NOTHING and says so. It is kept because the failure is the
# measurement: the refusal in the log above is reproducible, and the four names are the right targets
# for whoever gets `setAnalysisOption` to work under PyGhidra (a `java.lang.String` wrapper is the
# obvious next thing to try, and `analyzeHeadless -propertiesPath` is the route that avoids the
# Python-to-Java overload question entirely).

# GhidraScript already exposes `currentProgram` and `getAnalysisOptions`; importing them from a
# module is the shape that fails, and a pre-script that throws leaves auto-analysis running with the
# analyzers this file exists to disable. The import guard is the first thing that goes wrong, so it
# is worth stating: `from ghidra.framework import getAnalysisOptions` raises
# ClassNotFoundException under PyGhidra.

# Analyzer names, and what each one is turned off for. Named rather than wildcarded so an unknown
# name in a future Ghidra is a visible no-op in the log instead of a silent behaviour change.
DISABLED = (
    ("Decompiler Parameter ID",
     "invents function boundaries and parameter lists from call-site shape; on a flat raw binary it "
     "splits real function bodies and creates overlapping instructions"),
    ("Non-Returning Functions - Discovered",
     "guesses that a call does not return from its shape; every caller then decompiles truncated "
     "with a fabricated result, which is this workspace's signature failure mode"),
    ("Non-Returning Functions - Aggressive",
     "the same guess, applied more widely"),
    ("Reference",
     "on a flat raw binary the code/data split is unknown, so this propagates data references "
     "through what is actually code"),
)


def _log(message):
    print("[decomp-prescript] " + message)


# The analysis options for the open program. Reaching them is spelled out because every obvious
# spelling is wrong on this Ghidra, and a pre-script that throws leaves auto-analysis running with
# the analyzers this file exists to disable -- a silent no-op that looks like a clean run:
#   from ghidra.framework import getAnalysisOptions  -> ClassNotFoundException
#   getAnalysisOptions(currentProgram)                -> NameError (not injected into the namespace)
#   currentProgram.getOptions(programContext)          -> no matching overloads
#   currentProgram.getAnalysisOptions()                -> no such attribute on ProgramDB
# The one that works is the AnalysisOptions published by the analysis option OWNER, which is the
# program context -- the same object every one of those three attempts was reaching for by the wrong
# route. `setAnalysisOption` is the GhidraScript helper and needs no options object at all, so it
# is what the loop below uses; this lookup exists only to REPORT which options are actually known,
# because "disabled 4 of 4" and "disabled 0 of 4, by unknown name" must not print the same line.
options = None
def known_options():
    """Every analyzer option name this Ghidra knows, so a no-op cannot read as a change.

    `analyzeHeadless` accepts `-preScript` but NOT a list of options, so the names have to come from
    the running program. `AutoAnalysisManager` is the owner of the analysis options and is
    constructible from the current tool, which is what a script has.
    """
    try:
        from ghidra.app.analyze.AutoAnalysisManager import AutoAnalysisManager  # noqa: PLC0415
        manager = AutoAnalysisManager(currentProgram)
        return set(manager.getAnalysisOptions().getOptionNames())
    except Exception as error:  # noqa: BLE001
        _log(f"could not enumerate analyzer option names ({error}); reporting each disable by name "
             "and result instead")
        return None


changed = 0
available = known_options()
for name, reason in DISABLED:
    if available is not None and name not in available:
        _log(f"analyzer '{name}' is NOT a known option in this Ghidra -- it was not disabled, and the "
             "post-script's anchor check is what catches the consequence")
        continue
    try:
        setAnalysisOption(name, "false")
        _log(f"disabled analyzer '{name}': {reason}")
        changed += 1
    except Exception as error:  # noqa: BLE001 - an unknown analyzer name must not stop the run
        _log(f"could not disable '{name}' ({error}); left as Ghidra ships it, and the post-script's "
             "anchor check is what catches the consequence")

# The aggressive instruction finder, off for the same reason: it is what decides that a byte range
# not yet reached is the start of a new instruction, and on a flat binary that decision is a guess.
for option in ("Non-Returning Functions", "Aggressive Instruction Finder"):
    if available is not None and option not in available:
        _log(f"option '{option}' is NOT known in this Ghidra; not disabled")
        continue
    try:
        setAnalysisOption(option, "false")
        _log(f"disabled option '{option}'")
        changed += 1
    except Exception as error:  # noqa: BLE001
        _log(f"option '{option}' unavailable ({error})")

_log(f"{changed} analyzer option(s) turned off.")
if changed == 0:
    # THIS SCRIPT DID NOT DO ITS JOB, and it must not let the run proceed as if it had. A pre-script
    # cannot fail the headless run, so the statement has to be in the log AND the consequence has to
    # be refused downstream: `decomp_manifest` rule 5 refuses a manifest whose noreturn guard was
    # inert, and the anchor check refuses a body that stops short of the values the image says are in
    # it. Measured 2026-09-28: with 0 of 4 disables applied, the control 0x800258F0 still decompiles
    # WITHOUT the environment-animation phase and IS refused. So the refusal is the protection, and
    # this line only has to be honest about the cause.
    _log("NOTHING WAS DISABLED. The auto-analysis that produced this project still includes the "
         "analyzers named above, so its function boundaries and its no-return opinions are still "
         "guesses. Any decompile of it must be treated as suspect until the anchor check admits it, "
         "and a body the anchor check refuses is very likely one of these analyzers' doing rather "
         "than a decompiler limitation. Do not read this run's bodies as evidence.")
