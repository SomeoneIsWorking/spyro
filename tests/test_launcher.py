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
            resolve_step=lambda explicit: events.append(("resolve", explicit)) or disc,
            discdump_step=lambda framework, cc, cxx: events.append(
                ("discdump", framework, cc, cxx)
            )
            or discdump,
            provision_step=lambda media, framework, tool: events.append(
                ("provision", media, framework, tool)
            ),
            build_step=lambda framework, cc, cxx: events.append(
                ("build", framework, cc, cxx)
            ),
            launch_step=lambda framework, media: events.append(("launch", framework, media)),
        )

        self.assertEqual(events[0:2], ["preflight", "framework"])
        self.assertEqual(events[2], ("submodules", psxport))
        self.assertEqual(events[3], ("resolve", None))
        self.assertEqual(events[-1], ("launch", psxport, disc))
        self.assertEqual(launcher.PORT, ROOT / "scratch/bin/spyro_port")
        self.assertEqual(launcher.EXE, ROOT / "scratch/bin/spyro/SCUS_942.28")

    def test_explicit_missing_disc_refuses(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            launcher.resolve_disc(ROOT / "missing-disc.chd")
        self.assertEqual(raised.exception.code, 1)

    def test_refusal_stops_before_build_and_launch(self):
        events = []

        def refuse(_explicit):
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


if __name__ == "__main__":
    unittest.main()
