#!/usr/bin/env python3
"""Shipping-sequence and refusal tests for tools/run.py."""

import contextlib
import io
import os
import subprocess
import sys
import unittest
from collections.abc import Sequence
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import run as launcher  # noqa: E402

LOCKED_PYTHON = "/locked/venv/bin/python"


class FakeHost(launcher.Host):
    def __init__(
        self,
        *,
        missing: set[str] | None = None,
        missing_module: str | None = None,
        system: str = "Linux",
        distribution: str = "fedora",
    ):
        self.missing = missing or set()
        self.missing_module = missing_module
        self.system_name = system
        self.distribution = distribution
        self.commands: list[list[str]] = []

    def which(self, name: str) -> str | None:
        return None if name in self.missing else f"/fake/{name}"

    def system(self) -> str:
        return self.system_name

    def linux_distribution(self) -> str:
        return self.distribution

    def run(
        self, args: Sequence[str], **_kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        command = [str(value) for value in args]
        self.commands.append(command)
        returncode = int(
            self.missing_module is not None and self.missing_module in command
        )
        return subprocess.CompletedProcess(command, returncode)


class LauncherTest(unittest.TestCase):
    def test_help_exits_before_shipping_discovery(self):
        for option in ("-h", "--help"):
            with self.subTest(option=option), mock.patch.object(
                launcher, "execute"
            ) as execute, contextlib.redirect_stdout(io.StringIO()) as output:
                with self.assertRaises(SystemExit) as stopped:
                    launcher.main([option])
                self.assertEqual(stopped.exception.code, 0)
                self.assertIn("usage:", output.getvalue().lower())
                execute.assert_not_called()

    def test_no_argument_sequence_reaches_current_target(self):
        events = []
        psxport = ROOT / "external/psxport"
        disc = ROOT / "disc.chd"
        discdump = ROOT / "scratch/bin/discdump"

        launcher.execute(
            None,
            preflight_step=lambda: events.append("preflight")
            or ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"],
            sync_step=lambda: events.append("framework") or psxport,
            submodule_step=lambda framework: events.append(("submodules", framework)),
            resolve_step=lambda spec, explicit: events.append(("resolve", spec.slug, explicit))
            or disc,
            discdump_step=lambda framework, compiler_options: events.append(
                ("discdump", framework, compiler_options)
            )
            or discdump,
            provision_step=lambda spec, media, framework, tool: events.append(
                ("provision", spec.slug, media, framework, tool)
            )
            or launcher.EXE,
            build_step=lambda framework, compiler_options: events.append(
                ("build", framework, compiler_options)
            ),
            launch_step=lambda framework, media, spec, executable: events.append(
                ("launch", framework, media, spec.slug, executable)
            ),
        )

        self.assertEqual(events[0:2], ["preflight", "framework"])
        self.assertEqual(events[2], ("submodules", psxport))
        self.assertEqual(events[3], ("resolve", "spyro1", None))
        self.assertEqual(events[-1], ("launch", psxport, disc, "spyro1", launcher.EXE))
        self.assertEqual(launcher.PORT, ROOT / "scratch/bin/spyro_port")
        self.assertEqual(launcher.EXE, ROOT / "scratch/bin/spyro/SCUS_942.28")

    def test_explicit_missing_disc_refuses(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaisesRegex(
            launcher.Refusal, "not a file"
        ):
            launcher.resolve_disc(
                launcher.provision_title.SPECS["spyro1"], ROOT / "missing-disc.chd"
            )

    def test_refusal_stops_before_build_and_launch(self):
        events = []

        def refuse(_spec, _explicit):
            events.append("refuse")
            raise launcher.Refusal("missing media")

        with self.assertRaisesRegex(launcher.Refusal, "missing media"):
            launcher.execute(
                None,
                preflight_step=lambda: [],
                sync_step=lambda: ROOT / "external/psxport",
                submodule_step=lambda _framework: None,
                resolve_step=refuse,
                discdump_step=lambda *_args: events.append("discdump"),
                provision_step=lambda *_args: events.append("provision"),
                build_step=lambda *_args: events.append("build"),
                launch_step=lambda *_args: events.append("launch"),
            )
        self.assertEqual(events, ["refuse"])

    def test_title_codeword_reaches_only_its_manifest_and_executable(self):
        events = []
        spec = launcher.provision_title.SPECS["spyro3"]
        disc = ROOT / "spyro3.chd"
        executable = spec.cache_dir / spec.serial

        launcher.execute(
            str(disc),
            title="spyro3",
            preflight_step=lambda: [],
            sync_step=lambda: ROOT / "external/psxport",
            submodule_step=lambda _framework: None,
            resolve_step=lambda selected, explicit: events.append(
                ("resolve", selected.slug, explicit)
            )
            or disc,
            discdump_step=lambda *_args: ROOT / "scratch/bin/discdump",
            provision_step=lambda selected, *_args: events.append(
                ("provision", selected.slug)
            )
            or executable,
            build_step=lambda *_args: None,
            launch_step=lambda _framework, _media, selected, selected_executable: events.append(
                ("launch", selected.slug, selected_executable)
            ),
        )

        self.assertEqual(
            events,
            [
                ("resolve", "spyro3", str(disc)),
                ("provision", "spyro3"),
                ("launch", "spyro3", executable),
            ],
        )

    def test_player_launch_environment_strips_ambient_agent_policy(self):
        psxport = ROOT / "external/psxport"
        disc = ROOT / "disc.chd"
        with mock.patch.dict(
            os.environ,
            {
                "PSXPORT_NOWINDOW": "1",
                "PSXPORT_VK_HEADLESS": "1",
                "PSXPORT_NOAUDIO": "1",
                "PSXPORT_NOPACE": "1",
                "PSXPORT_DEBUG_SERVER": "0",
            },
            clear=True,
        ):
            env = launcher.launch_environment(psxport, disc)
        self.assertEqual(env["PSXPORT_VK_WINDOW"], "1")
        for key in (
            "PSXPORT_NOWINDOW",
            "PSXPORT_VK_HEADLESS",
            "PSXPORT_NOAUDIO",
            "PSXPORT_NOPACE",
        ):
            self.assertNotIn(key, env)
        self.assertEqual(env["PSXPORT_DEBUG_SERVER"], "0")
        self.assertEqual(env["PSXPORT_ASSET_DIR"], str(psxport))
        self.assertEqual(env["PSXPORT_SPYRO_DISC"], str(disc))
        self.assertEqual(env["PSXPORT_DISC"], str(disc))

    def test_prepare_only_never_launches_or_runs_tests(self):
        events = []
        launcher.execute(
            None,
            prepare_only=True,
            preflight_step=lambda: [],
            sync_step=lambda: ROOT / "external/psxport",
            submodule_step=lambda _framework: None,
            resolve_step=lambda _spec, _explicit: ROOT / "disc.chd",
            discdump_step=lambda *_args: ROOT / "scratch/bin/discdump",
            provision_step=lambda *_args: launcher.EXE,
            build_step=lambda *_args: events.append("build"),
            launch_step=lambda *_args: events.append("launch"),
        )
        self.assertEqual(events, ["build"])

    def test_missing_dependencies_print_exact_platform_commands(self):
        cases = (
            (FakeHost(missing={"cmake"}), "sudo dnf install cmake"),
            (
                FakeHost(missing={"pkg-config"}, distribution="ubuntu"),
                "sudo apt install pkg-config",
            ),
            (FakeHost(missing={"git"}, system="Darwin"), "xcode-select --install"),
        )
        for host, expected in cases:
            with self.subTest(expected=expected), self.assertRaisesRegex(
                launcher.Refusal, expected
            ):
                launcher.preflight(host, {})

    def test_missing_native_library_prints_exact_dnf_command(self):
        with self.assertRaisesRegex(
            launcher.Refusal, "sudo dnf install SDL3-devel"
        ):
            launcher.preflight(FakeHost(missing_module="sdl3"), {})

    def test_compilers_are_forwarded_without_identity_checks(self):
        host = FakeHost(missing={"clang", "clang++"})
        self.assertEqual(
            launcher.compiler_arguments(
                host, {"CC": "custom-c", "CXX": "custom-cxx"}
            ),
            [
                "-DCMAKE_C_COMPILER=custom-c",
                "-DCMAKE_CXX_COMPILER=custom-cxx",
            ],
        )
        self.assertEqual(launcher.compiler_arguments(host, {}), [])

    def test_configure_uses_locked_python_and_disables_ctest(self):
        commands = []
        with mock.patch.object(
            launcher,
            "command",
            side_effect=lambda args, **_kwargs: commands.append(args),
        ), mock.patch.object(launcher.sys, "executable", LOCKED_PYTHON):
            launcher.configure(ROOT, ROOT / "scratch/build/launcher-test", [])
        configure = commands[0]
        self.assertIn(f"-DPython3_EXECUTABLE={LOCKED_PYTHON}", configure)
        self.assertIn("-DBUILD_TESTING=OFF", configure)
        self.assertNotIn("ctest", [Path(str(value)).name for value in configure])

    def test_maintainer_configure_can_enable_tests_explicitly(self):
        commands = []
        with mock.patch.object(
            launcher,
            "command",
            side_effect=lambda args, **_kwargs: commands.append(args),
        ), mock.patch.object(launcher.sys, "executable", LOCKED_PYTHON):
            launcher.configure(
                ROOT,
                ROOT / "scratch/build/launcher-test",
                [],
                build_testing=True,
            )
        self.assertIn("-DBUILD_TESTING=ON", commands[0])

    def test_player_build_is_isolated_and_targets_only_the_port(self):
        commands = []
        with mock.patch.object(
            launcher,
            "configure",
            side_effect=lambda *args: commands.append(["configure", *args]),
        ), mock.patch.object(
            launcher,
            "command",
            side_effect=lambda args, **_kwargs: commands.append(args),
        ), mock.patch.object(launcher.os, "access", return_value=True):
            launcher.configure_and_build(ROOT / "external/psxport", [])

        self.assertEqual(commands[0][2], launcher.PLAYER_BUILD)
        self.assertEqual(
            commands[1][0:5],
            ["cmake", "--build", launcher.PLAYER_BUILD, "--target", "spyro_port"],
        )
        self.assertEqual(launcher.PLAYER_BUILD, ROOT / "scratch/build/player")
        flattened = [str(value) for command in commands for value in command]
        self.assertNotIn("ctest", [Path(value).name for value in flattened])

    def test_shell_and_locked_project_are_the_stable_entry_contract(self):
        self.assertEqual(
            (ROOT / "run.sh").read_text(),
            '#!/bin/sh\ncd "$(dirname "$0")" || exit 1\n'
            'exec uv run --frozen python bootstrap.py "$@"\n',
        )
        self.assertIn("from tools.run import main", (ROOT / "bootstrap.py").read_text())
        self.assertIn("package = false", (ROOT / "pyproject.toml").read_text())
        self.assertIn("version = 1", (ROOT / "uv.lock").read_text())
        self.assertTrue(os.access(ROOT / "run.sh", os.X_OK))


if __name__ == "__main__":
    unittest.main()
