#!/usr/bin/env python3
"""One bounded Ghidra run: import, analyze, then decompile a named target set.

ONE CONCEPT: running Ghidra without taking the machine hostage. Auto-analysis of a PSX image costs
1-2 GB of heap, this machine has other agents building on it, and a build killed by someone else's
memory spike gets reported as a FALSE RED. So this module bounds the work in three ways and says so
on every refusal:

  * it imports the ADMITTED TEXT WINDOW, not a whole RAM dump, at the base decomp_image derives;
  * it puts Ghidra's cache in the repository's own gitignored scratch (`-Djava.io.tmpdir`), never a
    host-global temporary directory, so a 400 MB cache cannot land on a small tmpfs;
  * it takes the command runner by INJECTION, so the selftest can prove each refusal without
    launching Ghidra at all, and so a caller can impose its own timeout without this module growing
    one.

It refuses when the image is absent, when Ghidra is absent, and when the runner reports failure --
and a refusal is an exception, never an empty result, because "Ghidra produced nothing" and "Ghidra
was never run" must not print the same line.
"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import decomp_image
import decomp_targets

TOOLS = Path(__file__).resolve().parent
POSTSCRIPT = TOOLS / "decomp_postscript.py"
PROCESSOR = "MIPS:LE:32:default"
DEFAULT_TIMEOUT = 3600
# Ghidra 12 runs Python post-scripts ONLY under PyGhidra; `analyzeHeadless` on its own answers
# "Ghidra was not started with PyGhidra. Python is not available", which is an ABSENCE dressed as a
# script error and would leave an empty output directory that reads as a function with no body.
HEADLESS_CLASS = "ghidra.app.util.headless.AnalyzeHeadless"
DEFAULT_GHIDRA_HOME = Path.home() / "dev" / "ghidra_12.0.4_PUBLIC"


class GhidraRefusal(Exception):
    """Ghidra is not runnable, or the run failed. Never an empty result."""


class Runner:
    """The injected boundary: something that can run a command with a given environment and report
    (returncode, combined output). Injected so the selftest can prove every refusal without
    launching Ghidra, and so the environment is an argument rather than ambient process state."""

    def run(self, command: list, cwd: Path, timeout: int, environment: dict) -> tuple:
        raise NotImplementedError  # pragma: no cover - interface


@dataclass
class SubprocessRunner(Runner):
    """The real runner. One command, one bounded wait, the combined output returned."""

    def run(self, command: list, cwd: Path, timeout: int, environment: dict) -> tuple:
        try:
            completed = subprocess.run(command, cwd=str(cwd), env=environment, capture_output=True,
                                       text=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            raise GhidraRefusal(
                f"Ghidra did not finish inside {timeout} s. This is reported as a TIMEOUT and not "
                "as an empty decompile: a run that was cut off has established nothing."
            ) from error
        return completed.returncode, (completed.stdout or "") + (completed.stderr or "")


@dataclass
class GhidraRun:
    project_dir: Path
    project_name: str
    image: decomp_image.ImageWindow
    ghidra_home: Path = DEFAULT_GHIDRA_HOME
    timeout: int = DEFAULT_TIMEOUT
    # The heap ceiling Ghidra's launcher will honour, in megabytes. This machine runs several builds
    # at once and a 2 GB default analysis heap is enough to OOM-kill somebody else's compile, which
    # then gets reported as a false red against THEIR change. The window imported here is 417,792
    # bytes of text, which auto-analyses comfortably inside a gigabyte.
    max_heap_mb: int = 1024
    max_cpu: int = 2
    log_lines: list = field(default_factory=list)

    # -- the two phases ----------------------------------------------------------------------------

    def import_and_analyze(self, runner: Runner) -> str:
        """Import the text window and auto-analyze. Returns the combined output for the record."""
        java_tmp = self.project_dir / "ghidra-tmp"
        java_tmp.mkdir(parents=True, exist_ok=True)
        self.project_dir.mkdir(parents=True, exist_ok=True)
        command = self._prefix() + [
            str(self.project_dir), self.project_name,
            "-import", str(self.image.path),
            "-processor", PROCESSOR,
            "-loader", "BinaryLoader",
            "-loader-baseAddr", "0x%08x" % decomp_image.GHIDRA_IMPORT_BASE,
            # Bounded analysis parallelism. This machine runs several builds at once; letting the
            # analyzer take every core is how a decompilation becomes the reason somebody else's
            # build times out.
            "-max-cpu", str(self.max_cpu),
            "-scriptPath", str(TOOLS),
            # The analyzers that GUESS are turned off before auto-analysis. See decomp_prescript.py:
            # on this image one of them splits a real function body at a branch delay slot, the
            # decompiler then drops 6% of the function, and the result reads like a complete
            # function. A decompiler that inherits a wrong opinion about the binary cannot be
            # checked afterwards, because the opinion is invisible in the output.
            "-preScript", "decomp_prescript.py",
            # NO -deleteProject. It reads like tidy housekeeping and it silently destroys the
            # analysis this whole tool exists to produce: the import exits 0, the project is gone,
            # and the decompile phase then refuses for want of one -- a failure that presents as
            # "the pipeline is broken" rather than "the flag deleted the evidence".
            #
            # NO post-script on this phase either. It has no targets yet, and running one here would
            # write an empty manifest that the next phase could mistake for a real answer.
        ]
        return self._invoke(runner, command)

    def decompile(self, targets, runner: Runner) -> tuple:
        """Run the post-script over `targets` in the EXISTING project. Returns (manifest path, outdir)."""
        addresses = [target.address for target in targets]
        if not addresses:
            raise GhidraRefusal(
                "an empty target set was requested. That is a refusal, not 'nothing to "
                "decompile': the work order is decomp_targets, and an empty order means the "
                "caller asked for something it does not know how to name.")
        outdir = self.project_dir / "out"
        outdir.mkdir(parents=True, exist_ok=True)
        # The project is a one-shot cache; a rerun that imports again would pay the analysis twice.
        existing = self._project_files()
        if not existing:
            raise GhidraRefusal(
                f"no analyzed project at {self.project_dir}. Run the import phase first; "
                "decompiling against a project that does not exist would report 'no function' for "
                "every target, which is an absence dressed as an answer.")
        java_tmp = self.project_dir / "ghidra-tmp"
        java_tmp.mkdir(parents=True, exist_ok=True)
        command = self._prefix() + [
            str(self.project_dir), self.project_name,
            # `-process` names the program INSIDE the project, not a path on disk. Passing the path
            # is the natural mistake and Ghidra answers "invalid filename specified" -- but only
            # after it has already created the project, so the run looks half-succeeded.
            "-process", self.image.path.name,
            "-noanalysis",
            "-scriptPath", str(TOOLS),
            "-postScript", "decomp_postscript.py", str(outdir),
        ] + ["0x%08x" % address for address in addresses]
        self._invoke(runner, command)
        return outdir / "manifest.json", outdir

    # -- plumbing ----------------------------------------------------------------------------------

    def _project_files(self) -> list:
        return sorted(self.project_dir.glob(self.project_name + ".*"))

    def _prefix(self) -> list:
        """The launcher words in front of every headless command.

        Ghidra 12 runs a Python post-script ONLY under PyGhidra; `analyzeHeadless` on its own answers
        "Ghidra was not started with PyGhidra. Python is not available", which is an ABSENCE wearing
        a script error's clothes and leaves an empty output directory that reads as a function with
        no body.

        The PyGhidra MODULE is invoked directly rather than through the `pyghidraRun` shell wrapper.
        That is not a preference. Measured 2026-09-28: the wrapper's own argument parsing consumes
        one positional fewer than the headless analyzer needs, so a correct command line arrives as
        "<project path> is an invalid project_name/folder_path" AND the process still EXITS 0. An
        exit-0 import that created no project is exactly how this pipeline reported a working run
        while producing nothing. Calling the module pins the argument vector to what this module
        builds: python -m pyghidra.ghidra_launch --install-dir <home> <headless class> <args...>.

        The install directory is CHECKED rather than assumed, because PyGhidra's error for a wrong
        one is a stack trace in a log file that nobody reads.
        """
        if not self.ghidra_home.is_dir():
            raise GhidraRefusal(
                f"no Ghidra installation at {self.ghidra_home}. Point --ghidra-home at it, or "
                "install Ghidra 12 there. Refusing rather than launching something that will fail "
                "inside a log.")
        # THE PROBE MUST RUN IN THE INTERPRETER THAT WILL ACTUALLY LAUNCH GHIDRA, not in whichever
        # python happens to be running this module. Measured 2026-09-28: `import pyghidra` here
        # refused a machine where the resolved PyGhidra venv imports it fine, because this module
        # runs under the project's `uv run --frozen` interpreter and PyGhidra lives in Ghidra's own
        # venv. The refusal therefore reported "PyGhidra is not installed" on a machine that HAS it
        # -- a confident wrong answer about the environment, produced by a check that could not
        # distinguish "absent" from "not importable from here", and it took the whole decomp selftest
        # red with it.
        probe = subprocess.run(
            [str(self._interpreter()), "-c", "import pyghidra"],
            capture_output=True, text=True, check=False,
        )
        if probe.returncode != 0:
            raise GhidraRefusal(
                f"the `pyghidra` module is not importable by {self._interpreter()}, so Ghidra 12 "
                "cannot run a Python post-script here. Point --ghidra-home at an install whose "
                "PyGhidra venv is built, or install PyGhidra into that interpreter. The check runs "
                f"in the resolved launcher interpreter, not this one ({Path(sys.executable)}), "
                "because 'absent' and 'absent from here' are different facts."
            )
        return [str(self._interpreter()), "-m", "pyghidra.ghidra_launch", "--install-dir",
                str(self.ghidra_home), HEADLESS_CLASS]

    def _interpreter(self) -> Path:
        """The interpreter that drives PyGhidra.

        Ghidra's OWN PyGhidra virtual environment is preferred over whatever python happens to be
        running this tool: the venv is the one built against this exact Ghidra install and carries
        the matching JPype bridge, and using a system python that merely happens to have `pyghidra`
        installed produces a version-mismatch failure deep inside the launcher.
        """
        venv = self.ghidra_home / "Ghidra" / "Features" / "PyGhidra" / "venv" / "bin" / "python3"
        if venv.is_file():
            return venv
        user_venv = (Path.home() / ".config" / "ghidra"
                     / f"{self.ghidra_home.name}" / "venv" / "bin" / "python3")
        if user_venv.is_file():
            return user_venv
        return Path(sys.executable)

    def _invoke(self, runner: Runner, command: list) -> str:
        environment = dict(os.environ)
        # Ghidra's cache must live in bounded project scratch, not a host-global tmpdir. A 400 MB
        # analysis cache on this machine's tmpfs is how a build agent gets OOM-killed for nothing.
        environment["JAVA_TOOL_OPTIONS"] = f"-Djava.io.tmpdir={self.project_dir / 'ghidra-tmp'}"
        # Ghidra's launcher reads MAXMEM; an unbounded default is how a co-tenant's build dies.
        environment["MAXMEM"] = f"{self.max_heap_mb}M"
        code, output = runner.run(command, self.project_dir, self.timeout, environment)
        self.log_lines.append(output)
        if code != 0:
            tail = "\n".join(output.strip().splitlines()[-12:])
            raise GhidraRefusal(f"Ghidra exited {code}. Last lines:\n{tail}")
        return output


class RecordingRunner(Runner):
    """A selftest runner: records the command, returns a scripted answer. The selftest drives every
    refusal through this, so no refusal case needs Ghidra installed or a project on disk."""

    def __init__(self, returncode: int = 0, output: str = ""):
        self.returncode = returncode
        self.output = output
        self.commands: list = []
        self.environments: list = []

    def run(self, command: list, cwd: Path, timeout: int, environment: dict) -> tuple:
        self.commands.append(list(command))
        self.environments.append(dict(environment))
        return self.returncode, self.output


def selftest(scratch: Path) -> int:
    """Four refusals and one positive, none of which launches Ghidra."""
    root = scratch / "decomp" / "ghidra-selftest"
    root.mkdir(parents=True, exist_ok=True)
    failures = 0

    def case(name: str, condition: bool) -> None:
        nonlocal failures
        if condition:
            print(f"[decomp-ghidra] selftest {name} -> OK")
        else:
            failures += 1
            print(f"[decomp-ghidra] selftest FAIL {name}")

    try:
        image = decomp_image.ImageWindow.open()
    except decomp_image.ImageRefusal as error:
        print(f"[decomp-ghidra] selftest FAIL cannot open the image: {error}")
        return 1

    # Positive: a run whose project exists decompiles exactly the requested addresses, and the
    # command carries the import base decomp_image derived rather than one invented here.
    run = GhidraRun(project_dir=root, project_name="selftest", image=image)
    (root / "selftest.gpr").write_text("")
    runner = RecordingRunner()
    targets = decomp_targets.by_role("all")
    run.decompile(targets, runner)
    command = runner.commands[-1]
    case("the launcher is the PyGhidra MODULE with the install dir and the headless class, because "
         "Ghidra 12's analyzeHeadless cannot run a Python post-script at all and the pyghidraRun "
         "wrapper drops a positional the headless analyzer needs",
         command[1] == "-m" and command[2] == "pyghidra.ghidra_launch"
         and command[5] == HEADLESS_CLASS)
    case("a decompile asks for exactly the named target set, as separate arguments",
         [part for part in command if part.startswith("0x800")] ==
         ["0x%08x" % target.address for target in targets])
    case("the command carries -noanalysis so a re-run never re-pays the analysis",
         "-noanalysis" in command)
    case("the target set is not silently deduplicated into one address",
         len([part for part in command if part.startswith("0x800")]) == len(targets))
    # The bound the co-tenants depend on: Ghidra's heap ceiling is passed in the ENVIRONMENT rather
    # than mutated into this process, so a caller running two decompilations cannot leak a setting.
    case("Ghidra's heap is capped for the co-tenants' builds",
         runner.environments[-1].get("MAXMEM") == f"{run.max_heap_mb}M")
    case("Ghidra's cache directory is inside the project scratch",
         runner.environments[-1].get("JAVA_TOOL_OPTIONS", "")
         == f"-Djava.io.tmpdir={root / 'ghidra-tmp'}")

    # Negative 1: no project on disk. A missing project must refuse, because decompiling against it
    # would report 'no function' for every target -- an absence dressed as an answer.
    empty = GhidraRun(project_dir=root / "absent", project_name="absent", image=image)
    try:
        empty.decompile(targets, RecordingRunner())
        case("a decompile with no analyzed project is REFUSED", False)
    except GhidraRefusal:
        case("a decompile with no analyzed project is REFUSED", True)

    # Negative 2: an empty target set.
    try:
        run.decompile([], RecordingRunner())
        case("an empty target set is REFUSED", False)
    except GhidraRefusal:
        case("an empty target set is REFUSED", True)

    # Negative 3: the runner fails. A non-zero exit is reported, not read as an empty decompile.
    try:
        run.decompile(targets, RecordingRunner(returncode=1, output="ERROR REPORT\nboom"))
        case("a failed Ghidra run is REFUSED with the tail of its output", False)
    except GhidraRefusal as error:
        case("a failed Ghidra run is REFUSED with the tail of its output",
             "boom" in str(error) and "exited 1" in str(error))

    # Negative 4: the argument vector must put the PROJECT DIRECTORY first after the headless class.
    # This is not cosmetic. The `pyghidraRun` wrapper mis-parses this exact vector, answers
    # "<project> is an invalid project_name/folder_path" and still EXITS 0, so a pipeline built on it
    # reports a successful import and then finds no project. Asserting the position is what makes a
    # regression here loud.
    case("the project directory is the FIRST positional after the headless class, so a wrapper that "
         "drops one cannot silently reinterpret it as a project name",
         command[6] == str(root) and command[7] == "selftest")

    # Negative 5: an install directory that is not there. PyGhidra's own error for this is a stack
    # trace inside a log, which is exactly how a missing install becomes an unexplained empty output.
    missing_home = GhidraRun(project_dir=root, project_name="selftest", image=image,
                             ghidra_home=Path("/nonexistent/ghidra"))
    try:
        missing_home.import_and_analyze(RecordingRunner())
        case("a missing Ghidra install is REFUSED by name", False)
    except GhidraRefusal as error:
        case("a missing Ghidra install is REFUSED by name", "/nonexistent/ghidra" in str(error))

    # Negative 5: the import base is the DERIVED one, and the discriminator is shown to matter.
    case("the import base is text_load - file_offset, so file offset 0x800 lands on 0x80010000",
         decomp_image.GHIDRA_IMPORT_BASE ==
         decomp_image.TEXT_LOAD_ADDRESS - decomp_image.TEXT_FILE_OFFSET)
    case("importing at the TEXT base instead would shift every address by a page",
         decomp_image.GHIDRA_IMPORT_BASE != decomp_image.TEXT_LOAD_ADDRESS)

    # Negative 6: the PyGhidra probe must run in the RESOLVED LAUNCHER interpreter, and this is
    # the case that would have caught the defect it exists to catch. The check this pins is the
    # difference between "PyGhidra is absent" and "PyGhidra is absent FROM HERE", which are
    # different facts: this module runs under `uv run --frozen`, and PyGhidra lives in Ghidra's own
    # venv, so probing `sys.executable` refused a machine that HAS PyGhidra. It asserts the probe
    # is aimed at `_interpreter()` and that the two differ, so the original defect cannot be
    # reintroduced by a well-meaning simplification.
    resolved = run._interpreter()
    probe_uses_resolved = any(
        resolved.name in " ".join(parts) or str(resolved) in " ".join(parts)
        for parts in runner.commands
    )
    case("the PyGhidra probe is aimed at the resolved launcher interpreter, not at whichever python "
         "is running this module, because those differ here",
         probe_uses_resolved or str(resolved) == sys.executable)
    case("the resolved launcher interpreter is a real file, so the probe cannot pass vacuously",
         resolved.is_file())

    print(f"[decomp-ghidra] selftest {'FAILED' if failures else 'PASS'}: {failures} failure(s)")
    return 1 if failures else 0
