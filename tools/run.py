#!/usr/bin/env python3
"""Provision, build, and launch the current Spyro the Dragon port target."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

import ensure_recomp

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
FRAMEWORK_BUILD = ROOT / "scratch/build/psxport"
EXE = ROOT / "scratch/bin/spyro/SCUS_942.28"
PORT = ROOT / "scratch/bin/spyro_port"


class Refusal(RuntimeError):
    """The requested run cannot be performed honestly."""


def say(message):
    print(f"[run] {message}", file=sys.stderr)


def command(args, *, env=None, quiet=False):
    result = subprocess.run(
        [str(value) for value in args],
        cwd=ROOT,
        env=env,
        stdout=subprocess.DEVNULL if quiet else None,
        check=False,
    )
    if result.returncode:
        raise Refusal(f"command failed ({result.returncode}): {' '.join(map(str, args))}")


def checked_clang(name, default):
    compiler = os.environ.get(name, default)
    if not shutil.which(compiler):
        raise Refusal(f"{name}={compiler} was not found")
    probe = subprocess.run(
        [compiler, "--version"], capture_output=True, text=True, check=False
    )
    if probe.returncode or "clang" not in (probe.stdout + probe.stderr).lower():
        raise Refusal(f"{name}={compiler} is not Clang")
    return compiler


def preflight():
    for tool in ("cmake", "git", "pkg-config"):
        if not shutil.which(tool):
            raise Refusal(f"{tool} was not found")
    if subprocess.run(["pkg-config", "--exists", "sdl3"], check=False).returncode:
        raise Refusal("SDL3 was not found by pkg-config (install SDL3-devel/libsdl3-dev)")
    return checked_clang("CC", "clang"), checked_clang("CXX", "clang++")


def git_output(psxport, *args):
    result = subprocess.run(
        ["git", "-C", str(psxport), *args],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def sync_framework():
    command([sys.executable, ROOT / "tools/psxport_sync.py", "--auto"])
    configured = os.environ.get("PSXPORT_DIR")
    psxport = Path(configured or ROOT / "external/psxport").resolve()
    if not (psxport / "cmake/psxport.cmake").is_file():
        raise Refusal(f"PSXPORT_DIR={psxport} is not a psxport checkout")
    head = git_output(psxport, "rev-parse", "--short", "HEAD") or "unknown"
    dirty = " +dirty" if git_output(psxport, "status", "--porcelain") else ""
    if configured:
        say(f"framework: *** {psxport} *** (DEV CLONE {head}{dirty}) — NOT the recorded pin")
    else:
        say(f"framework: external/psxport -> {psxport} @ {head}{dirty}")
    return psxport


def sync_submodules(psxport):
    if not (ROOT / ".gitmodules").is_file():
        return
    tool = psxport / "scripts/sync-submodules.sh"
    if tool.is_file():
        command([tool])
        return
    say("WARNING: framework submodule-sync tool is absent; Spyro reference submodules were not synced")


def resolve_disc(explicit):
    argv = [str(ROOT / "tools/run.py")]
    if explicit:
        argv.append(explicit)
    return Path(ensure_recomp.resolve_disc(argv)).resolve()


def cache_value(build, key):
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return ""
    prefix = f"{key}:"
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(prefix):
            return line.partition("=")[2]
    return ""


def configure(source, build, cc, cxx, *definitions):
    build.mkdir(parents=True, exist_ok=True)
    cached_cxx = cache_value(build, "CMAKE_CXX_COMPILER")
    cached_source = cache_value(build, "CMAKE_HOME_DIRECTORY")
    selection_file = build / ".spyro-toolchain"
    selection = f"CC={cc}\nCXX={cxx}\n"
    if selection_file.is_file():
        fresh = selection_file.read_text() != selection
    else:
        fresh = bool(cached_cxx and Path(cached_cxx).name != Path(cxx).name)
    fresh |= bool(cached_source and Path(cached_source).resolve() != source.resolve())
    if fresh:
        say(f"reconfiguring {build.relative_to(ROOT)} for the selected Clang toolchain")
        allowed = {BUILD.resolve(), FRAMEWORK_BUILD.resolve()}
        if build.resolve() not in allowed:
            raise Refusal(f"refusing to clean unexpected build directory {build}")
        shutil.rmtree(build)
        build.mkdir(parents=True)
    args = [
        "cmake",
        "-S",
        source,
        "-B",
        build,
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_CXX_COMPILER={cxx}",
        *definitions,
    ]
    command(args, quiet=True)
    selection_file.write_text(selection)


def verify_clang_build(build, target):
    compiler_files = sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if not compiler_files or not any(
        'CMAKE_CXX_COMPILER_ID "Clang"' in path.read_text(errors="replace")
        for path in compiler_files
    ):
        raise Refusal(f"the configured {target} build is not using Clang")


def build_discdump(psxport, cc, cxx):
    say("building libchdr + discdump (incremental)…")
    configure(psxport, FRAMEWORK_BUILD, cc, cxx)
    verify_clang_build(FRAMEWORK_BUILD, "discdump")
    command(
        [
            "cmake",
            "--build",
            FRAMEWORK_BUILD,
            "--target",
            "discdump",
            "-j",
            str(os.cpu_count() or 4),
        ],
        quiet=True,
    )
    for name in ("discdump", "discdump.exe"):
        candidate = FRAMEWORK_BUILD / "tools" / name
        if os.access(candidate, os.X_OK):
            return candidate
    raise Refusal("discdump build produced no executable")


def provision(disc, psxport, discdump):
    env = os.environ.copy()
    env["PSXPORT_DIR"] = str(psxport)
    env["PSXPORT_DISCDUMP"] = str(discdump)
    command([sys.executable, ROOT / "tools/ensure_recomp.py", disc], env=env)
    if not EXE.is_file():
        raise Refusal(f"recomp provisioning produced no {EXE.relative_to(ROOT)}")


def configure_and_build(psxport, cc, cxx):
    jobs = str(os.cpu_count() or 4)
    say(f"building the native port (CMake -j{jobs})…")
    configure(ROOT, BUILD, cc, cxx, f"-DPSXPORT_DIR={psxport}")
    verify_clang_build(BUILD, "spyro_port")
    command(["cmake", "--build", BUILD, "--target", "spyro_port", "-j", jobs])
    if not os.access(PORT, os.X_OK):
        raise Refusal(f"build produced no executable at {PORT.relative_to(ROOT)}")


def launch_environment(psxport, disc):
    env = os.environ.copy()
    if env.get("PSXPORT_NOWINDOW"):
        env["PSXPORT_VK_HEADLESS"] = "1"
    else:
        env["PSXPORT_VK_WINDOW"] = "1"
    env.setdefault("PSXPORT_ASSET_DIR", str(psxport))
    env.setdefault("PSXPORT_DEBUG_SERVER", "1")
    env["PSXPORT_SPYRO_DISC"] = str(disc)
    return env


def launch(psxport, disc):
    say("launching Spyro the Dragon (native PC port)…")
    os.execve(PORT, [str(PORT), str(EXE)], launch_environment(psxport, disc))


def execute(
    disc,
    *,
    preflight_step=preflight,
    sync_step=sync_framework,
    submodule_step=sync_submodules,
    resolve_step=resolve_disc,
    discdump_step=build_discdump,
    provision_step=provision,
    build_step=configure_and_build,
    launch_step=launch,
):
    """Run the shipping sequence; injectable steps let tests exercise refusal ordering."""
    cc, cxx = preflight_step()
    psxport = sync_step()
    submodule_step(psxport)
    resolved_disc = resolve_step(disc)
    say(f"disc: {resolved_disc}")
    discdump = discdump_step(psxport, cc, cxx)
    provision_step(resolved_disc, psxport, discdump)
    build_step(psxport, cc, cxx)
    launch_step(psxport, resolved_disc)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Provision, build, and launch the current Spyro the Dragon native PC port."
    )
    parser.add_argument("disc", nargs="?", help="Spyro (USA) CHD; otherwise use env/.env/drop-in")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        execute(args.disc)
    except (OSError, Refusal) as error:
        print(f"[run] error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
