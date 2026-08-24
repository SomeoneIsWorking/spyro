#!/usr/bin/env python3
"""Shipping-sequence and refusal tests for tools/run.py."""

import contextlib
import io
import os
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import run as launcher  # noqa: E402


class LauncherTest(unittest.TestCase):
    def test_no_argument_sequence_reaches_current_target(self):
        events = []
        psxport = ROOT / "external/psxport"
        disc = ROOT / "disc.chd"
        discdump = ROOT / "scratch/bin/discdump"

        launcher.execute(
            None,
            preflight_step=lambda: events.append("preflight") or ("clang", "clang++"),
            sync_step=lambda: events.append("framework") or psxport,
            submodule_step=lambda framework: events.append(("submodules", framework)),
            resolve_step=lambda spec, explicit: events.append(("resolve", spec.slug, explicit))
            or disc,
            discdump_step=lambda framework, cc, cxx: events.append(
                ("discdump", framework, cc, cxx)
            )
            or discdump,
            provision_step=lambda spec, media, framework, tool: events.append(
                ("provision", spec.slug, media, framework, tool)
            )
            or launcher.EXE,
            build_step=lambda framework, cc, cxx: events.append(
                ("build", framework, cc, cxx)
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
                preflight_step=lambda: ("clang", "clang++"),
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
            preflight_step=lambda: ("clang", "clang++"),
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

    def test_launch_environment_preserves_knobs_and_selects_headless(self):
        psxport = ROOT / "external/psxport"
        disc = ROOT / "disc.chd"
        with mock.patch.dict(
            os.environ,
            {
                "PSXPORT_NOWINDOW": "1",
                "PSXPORT_NOAUDIO": "1",
                "PSXPORT_DEBUG_SERVER": "0",
            },
            clear=True,
        ):
            env = launcher.launch_environment(psxport, disc)
        self.assertEqual(env["PSXPORT_VK_HEADLESS"], "1")
        self.assertEqual(env["PSXPORT_NOAUDIO"], "1")
        self.assertEqual(env["PSXPORT_DEBUG_SERVER"], "0")
        self.assertEqual(env["PSXPORT_ASSET_DIR"], str(psxport))
        self.assertEqual(env["PSXPORT_SPYRO_DISC"], str(disc))
        self.assertEqual(env["PSXPORT_DISC"], str(disc))


if __name__ == "__main__":
    unittest.main()
