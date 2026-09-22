#!/usr/bin/env python3
"""Provision, build, and launch the current Spyro the Dragon port target."""

from __future__ import annotations

import argparse
import os
import platform
import runpy
import shutil
import subprocess
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import TextIO

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import provision_title

ROOT = Path(__file__).resolve().parent.parent
PLAYER_BUILD = ROOT / "build/player"
FRAMEWORK_BUILD = ROOT / "build/player-tools"
MAINTAINER_BUILD = ROOT / "build"


class Refusal(RuntimeError):
    """The requested run cannot be performed honestly."""


class Host:
    """Narrow injectable seam around host discovery and process execution."""

    @staticmethod
    def which(name: str) -> str | None:
        return shutil.which(name)

    @staticmethod
    def run(args: Sequence[str], **kwargs: object) -> subprocess.CompletedProcess:
        check = bool(kwargs.pop("check", False))
        return subprocess.run([str(value) for value in args], check=check, **kwargs)

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
            "ninja": "brew install ninja",
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
            "ninja": "winget install Ninja-build.Ninja",
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
            "ninja": "sudo dnf install ninja-build",
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
            "ninja": "sudo apt install ninja-build",
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


def resolved_compiler(host: Host, requested: str) -> str:
    """Spell a chosen compiler the way CMake's cache will.

    CMake records the executable it found, not the token it was handed, and it compares that cache
    entry with the token on the next run. Passing a bare name therefore makes every later configure
    look like a toolchain change, and CMake answers that by deleting the cache: measured on this host,
    where a ccache shim precedes `/usr/bin` on PATH, `clang++` was recorded as
    `/usr/lib64/ccache/clang++`, the launcher's bare `clang++` disagreed with it on every run, and the
    self-triggered re-run silently finished on the system C++ compiler instead (see
    docs/findings/launcher-cmake-compiler-identity.md). Handing CMake the path it already holds ends
    that loop and keeps ccache in the driver chain.
    """

    if not requested or os.sep in requested or (os.altsep and os.altsep in requested):
        return requested
    return host.which(requested) or requested


def compiler_arguments(host: Host, environment: Mapping[str, str]) -> list[str]:
    """Pass user compiler choices through; otherwise prefer Clang if it is present."""

    arguments = []
    if cc := environment.get("CC"):
        arguments.append(f"-DCMAKE_C_COMPILER={resolved_compiler(host, cc)}")
    if cxx := environment.get("CXX"):
        arguments.append(f"-DCMAKE_CXX_COMPILER={resolved_compiler(host, cxx)}")
    if arguments:
        return arguments
    if host.which("clang") is not None and host.which("clang++") is not None:
        return [
            f"-DCMAKE_C_COMPILER={resolved_compiler(host, 'clang')}",
            f"-DCMAKE_CXX_COMPILER={resolved_compiler(host, 'clang++')}",
        ]
    return []


def preflight(
    host: Host | None = None, environment: Mapping[str, str] | None = None
) -> list[str]:
    machine = host or Host()
    for tool in ("cmake", "ninja", "git", "pkg-config"):
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
    """Initialize only dependencies required by this consumer.

    Beetle carries an optional URL-less oracle gitlink below its own checkout. Recursive sync
    therefore cannot be a fresh-clone prerequisite; the port needs Beetle, Lucent, and libchdr only.
    """
    command(
        [
            "git",
            "-C",
            psxport,
            "submodule",
            "update",
            "--init",
            "vendor/beetle-psx",
            "vendor/lucent",
        ]
    )
    command(
        [
            "git",
            "-C",
            Path(psxport) / "vendor/beetle-psx",
            "submodule",
            "update",
            "--init",
            "deps/libchdr",
        ]
    )


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


def owned_build_path(build: Path) -> Path:
    """Refuse aliases and unknown paths before creating or invalidating build state."""
    build = build.absolute()
    allowed = {PLAYER_BUILD.absolute(), FRAMEWORK_BUILD.absolute(), MAINTAINER_BUILD.absolute()}
    if build not in allowed or build.resolve() != build:
        raise Refusal(f"refusing unexpected or symlinked build directory {build}")
    return build


def same_compiler(left: str, right: str) -> bool:
    """Whether two compiler tokens name the same compiler.

    Two absolute paths are the same compiler only when they are equal; when either side is a bare
    name — the shape a hand-written cache holds — the basenames are compared, so respelling `clang++`
    as `/usr/lib64/ccache/clang++` (or the reverse) is never mistaken for a toolchain change. Symlinks
    are deliberately not followed: ccache dispatches on argv[0], so `/usr/lib64/ccache/clang` and
    `/usr/lib64/ccache/c++` are different compilers even though both resolve to one file.
    """

    left_path, right_path = Path(left), Path(right)
    if left_path.is_absolute() and right_path.is_absolute():
        return left_path == right_path
    return left_path.name == right_path.name


def option_value(options: Sequence[str], key: str) -> str:
    prefix = f"-D{key}="
    return next(
        (option.partition("=")[2] for option in options if option.startswith(prefix)), ""
    )


def compiler_mismatch(build: Path, compiler_options: Sequence[str]) -> str:
    """Name the first requested compiler the CMake cache does not record, or return "" when it agrees.

    The comparison is by compiler identity, so respelling `clang++` as `/usr/lib64/ccache/clang++`
    never looks like a toolchain change and never deletes a valid build; an actual switch, or CMake
    falling back to the system compiler, does.
    """

    for language in ("C", "CXX"):
        key = f"CMAKE_{language}_COMPILER"
        requested = option_value(compiler_options, key)
        recorded = cache_value(build, key)
        if not requested or not recorded:
            continue
        if not same_compiler(requested, recorded):
            return f"{key} is {recorded}, launcher requested {requested}"
    return ""


def reset_cmake_state(build: Path) -> None:
    """Invalidate this CMake tree without erasing sibling products or dependency builds."""
    build = owned_build_path(build)
    targets = [build / name for name in (
        "CMakeCache.txt", "CMakeFiles", "Makefile", "build.ninja", "rules.ninja",
        ".ninja_deps", ".ninja_log", "cmake_install.cmake", "CTestTestfile.cmake",
        "DartConfiguration.tcl", "CPackConfig.cmake", "CPackSourceConfig.cmake",
    )]
    for target in targets:
        if target.is_symlink():
            raise Refusal(f"refusing symlinked CMake state {target}")
    for target in targets:
        if target.is_dir():
            shutil.rmtree(target)
        else:
            target.unlink(missing_ok=True)


def configure(source, build, compiler_options, *definitions, build_testing=False):
    build = owned_build_path(Path(build))
    source = Path(source)
    cached_source = cache_value(build, "CMAKE_HOME_DIRECTORY")
    generator = cache_value(build, "CMAKE_GENERATOR")
    source_changed = bool(cached_source and Path(cached_source).resolve() != source.resolve())
    generator_changed = bool(generator and generator != "Ninja")
    # A compiler switch has to be applied to a cache-free tree: CMake deletes its own cache when the
    # requested compiler disagrees with it, and that self-triggered re-run does not put the requested
    # compiler back, so the tree can otherwise end up on the system compiler while the launcher keeps
    # asking for Clang. Resetting here makes the transition explicit and deterministic.
    mismatch = compiler_mismatch(build, compiler_options)
    if source_changed or generator_changed or mismatch:
        reason = mismatch or "source/Ninja configuration"
        say(f"resetting CMake state in {build.relative_to(ROOT)} for {reason}")
        reset_cmake_state(build)
    build.mkdir(parents=True, exist_ok=True)
    # The retired .spyro-toolchain stamp compared compiler argv text and let an alias or a ccache
    # spelling delete a valid build. Compiler identity is owned above instead, through resolved paths
    # and same_compiler, so only a real switch invalidates a tree; drop any stamp left behind.
    (build / ".spyro-toolchain").unlink(missing_ok=True)
    args = [
        "cmake",
        "-S",
        source,
        "-B",
        build,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DBUILD_TESTING={'ON' if build_testing else 'OFF'}",
        f"-DPython3_EXECUTABLE={sys.executable}",
        *compiler_options,
        *definitions,
    ]
    command(args, quiet=True)
    # Verify rather than assume. The failure this guards against is silent: the artifact links, runs,
    # and was produced by a compiler nobody selected.
    mismatch = compiler_mismatch(build, compiler_options)
    if mismatch:
        raise Refusal(f"CMake configured a different compiler than the launcher asked for: {mismatch}")


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


def provision(spec, disc, _psxport, discdump):
    try:
        return provision_title.provision(spec, disc, discdump)
    except provision_title.Refused as error:
        raise Refusal(str(error)) from error


def configure_and_build(psxport, compiler_options, spec=provision_title.SPECS["spyro1"]):
    jobs = str(os.cpu_count() or 4)
    say(f"building the native port (CMake -j{jobs})…")
    configure(ROOT, PLAYER_BUILD, compiler_options, f"-DPSXPORT_DIR={psxport}")
    target = "spyro_port"
    product = PLAYER_BUILD / "bin" / target
    command(["cmake", "--build", PLAYER_BUILD, "--target", target, "-j", jobs])
    if not os.access(product, os.X_OK):
        raise Refusal(f"build produced no executable at {product.relative_to(ROOT)}")


def launch_environment(psxport, disc, spec=provision_title.SPECS["spyro1"]):
    policy = runpy.run_path(str(Path(psxport) / "tools/port/launch_environment.py"))
    env = policy["player_environment"](os.environ, product=spec.slug)
    env.setdefault("PSXPORT_ASSET_DIR", str(psxport))
    env.setdefault("PSXPORT_DEBUG_SERVER", "1")
    env["PSXPORT_DISC"] = str(disc)
    for key in spec.env_keys:
        env[key] = str(disc)
    return env


def launch(psxport, disc, spec, executable):
    target = "spyro_port"
    product = PLAYER_BUILD / "bin" / target
    say(f"launching {spec.title} (native PC port)…")
    os.execve(
        product,
        [str(product), str(executable)],
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
    build_step(psxport, compiler_options, spec)
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
