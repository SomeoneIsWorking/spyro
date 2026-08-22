#!/usr/bin/env python3
"""Mechanical ownership gate for Spyro's framework seam."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class RuntimeStructureTest(unittest.TestCase):
    def test_runtime_is_the_behavior_owner(self):
        header = (ROOT / "game/core/spyro_runtime.h").read_text(encoding="utf-8")
        main = (ROOT / "game/core/main.cpp").read_text(encoding="utf-8")
        hooks = (ROOT / "game/core/game_hooks.cpp").read_text(encoding="utf-8")

        self.assertRegex(
            header,
            r"class\s+SpyroRuntime\s+final\s*:\s*public\s+LegacyGameRuntimeAdapter",
        )
        self.assertIn("psxport_install_game(runtime);", main)
        self.assertNotRegex(
            hooks,
            r"\.(ctxCreate|ctxDestroy|bootInit|registerOverrides)\s*=",
        )
        self.assertEqual(
            set(re.findall(r"\.(\w+)\s*=", hooks)),
            {"fps60WorldPass", "fps60TemporalRotate", "selftestGame", "fps60ReadSceneCam"},
        )


if __name__ == "__main__":
    unittest.main()
