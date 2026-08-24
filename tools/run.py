#!/usr/bin/env python3
"""Provision, build, and launch the current Spyro the Dragon port target."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import TextIO

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import ensure_recomp  # noqa: E402
import provision_title  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PLAYER_BUILD = ROOT / "scratch/build/player"
FRAMEWORK_BUILD = ROOT / "scratch/build/player-tools"
MAINTAINER_BUILD = ROOT / "build"
EXE = ROOT / "scratch/bin/spyro/SCUS_942.28"
PORT = ROOT / "scratch/bin/spyro_port"


class Refusal(RuntimeError):
    """The requested run cannot be performed honestly."""


class Host:
    """Narrow injectable seam around host discovery and process execution."""

    @staticmethod
    def which(name: str) -> str | None:
        return shutil.which(name)

    @staticmethod
    def run(args: Sequence[str], **kwargs: object) -> subprocess.CompletedProcess:
        return subprocess.run([str(value) for value in args], **kwargs)

    @staticmethod
    def system() -> str:
        return platform.system()

    @staticmethod
    def linux_distribution() -> str:
        try:
            values = {}
            for line in Path("/etc/os-release").read_text().splitlines():
                key, separator, value = line.partition("=")
                if separator:
                    values[key] = value.strip().strip('"').lower()
        except OSError:
            return "unknown"
        return " ".join((values.get("ID", ""), values.get("ID_LIKE", ""))).strip()


def say(message: str, stream: TextIO = sys.stderr) -> None:
    print(f"[run] {message}", file=stream)


def command(args: Sequence[object], *, env=None, quiet=False):
    result = subprocess.run(
        [str(value) for value in args],
        cwd=ROOT,
        env=env,
        stdout=subprocess.DEVNULL if quiet else None,
        check=False,
    )
    if result.returncode:
        raise Refusal(f"command failed ({result.returncode}): {' '.join(map(str, args))}")


def package_command(host: Host, package: str) -> str | None:
    system = host.system()
    if system == "Darwin":
        return {
            "cmake": "brew install cmake",
            "git": "xcode-select --install",
            "pkg-config": "brew install pkg-config",
            "sdl3": "brew install sdl3",
            "zlib": "brew install zlib",
            "openssl": "brew install openssl@3",
            "zstd": "brew install zstd",
        }[package]
    if system == "Windows":
        return {
            "cmake": "winget install Kitware.CMake",
            "git": "winget install Git.Git",
            "pkg-config": "vcpkg install pkgconf",
            "sdl3": "vcpkg install sdl3",
            "zlib": "vcpkg install zlib",
            "openssl": "vcpkg install openssl",
            "zstd": "vcpkg install zstd",
        }[package]
    if system != "Linux":
        return None

    distribution = set(host.linux_distribution().split())
    if distribution & {"fedora", "rhel", "centos", "rocky", "almalinux"}:
        return {
            "cmake": "sudo dnf install cmake",
            "git": "sudo dnf install git",
            "pkg-config": "sudo dnf install pkgconf-pkg-config",
            "sdl3": "sudo dnf install SDL3-devel",
            "zlib": "sudo dnf install zlib-devel",
            "openssl": "sudo dnf install openssl-devel",
            "zstd": "sudo dnf install libzstd-devel",
        }[package]
    if distribution & {"debian", "ubuntu", "linuxmint", "pop"}:
        return {
            "cmake": "sudo apt install cmake",
            "git": "sudo apt install git",
            "pkg-config": "sudo apt install pkg-config",
            "sdl3": "sudo apt install libsdl3-dev",
            "zlib": "sudo apt install zlib1g-dev",
            "openssl": "sudo apt install libssl-dev",
            "zstd": "sudo apt install libzstd-dev",
        }[package]
    return None


def missing_dependency(host: Host, name: str, package: str) -> Refusal:
    install = package_command(host, package)
    if install:
        return Refusal(f"{name} was not found. Install it with: {install}")
    system = host.system()
    distribution = host.linux_distribution() if system == "Linux" else "unknown"
    return Refusal(
        f"{name} was not found, and no package command is recorded for "
        f"{system}/{distribution}; tell us which supported platform/version and package path you use"
    )


def require_tool(host: Host, name: str) -> None:
    if host.which(name) is None:
        raise missing_dependency(host, name, name)


def require_library(host: Host, module: str, name: str, package: str) -> None:
    try:
        result = host.run(["pkg-config", "--exists", module], check=False)
    except OSError as error:
        raise Refusal(f"could not query {name}: {error}") from error
    if result.returncode:
        raise missing_dependency(host, name, package)


def compiler_arguments(host: Host, environment: Mapping[str, str]) -> list[str]:
    """Pass user compiler choices through; otherwise prefer Clang if it is present."""

    arguments = []
    if cc := environment.get("CC"):
        arguments.append(f"-DCMAKE_C_COMPILER={cc}")
    if cxx := environment.get("CXX"):
        arguments.append(f"-DCMAKE_CXX_COMPILER={cxx}")
    if arguments:
        return arguments
    if host.which("clang") is not None and host.which("clang++") is not None:
        return ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
    return []


def preflight(
    host: Host | None = None, environment: Mapping[str, str] | None = None
) -> list[str]:
    machine = host or Host()
    for tool in ("cmake", "git", "pkg-config"):
        require_tool(machine, tool)
    for module, name, package in (
        ("sdl3", "SDL3 development files", "sdl3"),
        ("zlib", "zlib development files", "zlib"),
        ("openssl", "OpenSSL development files", "openssl"),
        ("libzstd", "zstd development files", "zstd"),
    ):
        require_library(machine, module, name, package)
    return compiler_arguments(machine, os.environ if environment is None else environment)


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


def resolve_disc(spec, explicit):
    try:
        return provision_title.resolve_disc(spec, explicit).path
    except provision_title.Refused as error:
        raise Refusal(str(error)) from error


def cache_value(build, key):
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return ""
    prefix = f"{key}:"
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(prefix):
            return line.partition("=")[2]
    return ""


def configure(source, build, compiler_options, *definitions, build_testing=False):
    build.mkdir(parents=True, exist_ok=True)
    cached_source = cache_value(build, "CMAKE_HOME_DIRECTORY")
    selection_file = build / ".spyro-toolchain"
    selection = "\n".join(compiler_options) + "\n"
    fresh = selection_file.is_file() and selection_file.read_text() != selection
    fresh |= bool(cached_source and Path(cached_source).resolve() != source.resolve())
    if fresh:
        say(f"reconfiguring {build.relative_to(ROOT)} for the selected compiler settings")
        allowed = {
            PLAYER_BUILD.resolve(),
            FRAMEWORK_BUILD.resolve(),
            MAINTAINER_BUILD.resolve(),
        }
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
        f"-DBUILD_TESTING={'ON' if build_testing else 'OFF'}",
        f"-DPython3_EXECUTABLE={sys.executable}",
        *compiler_options,
        *definitions,
    ]
    command(args, quiet=True)
    selection_file.write_text(selection)


def build_discdump(psxport, compiler_options):
    say("building libchdr + discdump (incremental)…")
    configure(psxport, FRAMEWORK_BUILD, compiler_options)
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


def provision(spec, disc, psxport, discdump):
    if spec.slug != "spyro1":
        try:
            return provision_title.provision(spec, disc, discdump)
        except provision_title.Refused as error:
            raise Refusal(str(error)) from error
    env = os.environ.copy()
    env["PSXPORT_DIR"] = str(psxport)
    env["PSXPORT_DISCDUMP"] = str(discdump)
    command([sys.executable, ROOT / "tools/ensure_recomp.py", disc], env=env)
    if not EXE.is_file():
        raise Refusal(f"recomp provisioning produced no {EXE.relative_to(ROOT)}")
    return EXE


def configure_and_build(psxport, compiler_options):
    jobs = str(os.cpu_count() or 4)
    say(f"building the native port (CMake -j{jobs})…")
    configure(ROOT, PLAYER_BUILD, compiler_options, f"-DPSXPORT_DIR={psxport}")
    command(["cmake", "--build", PLAYER_BUILD, "--target", "spyro_port", "-j", jobs])
    if not os.access(PORT, os.X_OK):
        raise Refusal(f"build produced no executable at {PORT.relative_to(ROOT)}")


def launch_environment(psxport, disc, spec=provision_title.SPECS["spyro1"]):
    env = os.environ.copy()
    if env.get("PSXPORT_NOWINDOW"):
        env["PSXPORT_VK_HEADLESS"] = "1"
    else:
        env["PSXPORT_VK_WINDOW"] = "1"
    env.setdefault("PSXPORT_ASSET_DIR", str(psxport))
    env.setdefault("PSXPORT_DEBUG_SERVER", "1")
    env["PSXPORT_DISC"] = str(disc)
    for key in spec.env_keys:
        env[key] = str(disc)
    return env


def launch(psxport, disc, spec, executable):
    say(f"launching {spec.title} (native PC port)…")
    os.execve(
        PORT,
        [str(PORT), str(executable)],
        launch_environment(psxport, disc, spec),
    )


def execute(
    disc,
    *,
    title="spyro1",
    preflight_step=preflight,
    sync_step=sync_framework,
    submodule_step=sync_submodules,
    resolve_step=resolve_disc,
    discdump_step=build_discdump,
    provision_step=provision,
    build_step=configure_and_build,
    launch_step=launch,
    prepare_only=False,
):
    """Run the shipping sequence; injectable steps let tests exercise refusal ordering."""
    spec = provision_title.SPECS[title]
    compiler_options = preflight_step()
    psxport = sync_step()
    submodule_step(psxport)
    resolved_disc = resolve_step(spec, disc)
    say(f"disc: {resolved_disc}")
    discdump = discdump_step(psxport, compiler_options)
    executable = provision_step(spec, resolved_disc, psxport, discdump)
    build_step(psxport, compiler_options)
    if prepare_only:
        say(f"{spec.title} is built and ready.")
        return
    launch_step(psxport, resolved_disc, spec, executable)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Provision, build, and launch a serial-identified Spyro native PC port."
    )
    parser.add_argument(
        "--title",
        choices=sorted(provision_title.SPECS),
        default="spyro1",
        help="engine-lineage title codeword (default: spyro1)",
    )
    parser.add_argument("disc", nargs="?", help="Spyro (USA) CHD; otherwise use env/.env/drop-in")
    parser.add_argument(
        "--prepare-only",
        action="store_true",
        help="provision and build the selected target without launching it",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        execute(args.disc, title=args.title, prepare_only=args.prepare_only)
    except (OSError, Refusal) as error:
        print(f"[run] error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
