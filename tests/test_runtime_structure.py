#!/usr/bin/env python3
"""Mechanical ownership gate for Spyro's framework seam."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class RuntimeStructureTest(unittest.TestCase):
    def test_runtime_is_the_behavior_owner(self):
        lineage = (ROOT / "game/core/spyro_runtime.h").read_text(encoding="utf-8")
        spyro1 = (ROOT / "titles/spyro1/core/spyro1_runtime.h").read_text(
            encoding="utf-8"
        )
        spyro2 = (ROOT / "titles/spyro2/core/spyro2_runtime.h").read_text(
            encoding="utf-8"
        )
        spyro2_source = (ROOT / "titles/spyro2/core/spyro2_runtime.cpp").read_text(
            encoding="utf-8"
        )
        spyro3 = (ROOT / "titles/spyro3/core/spyro3_runtime.h").read_text(
            encoding="utf-8"
        )
        spyro3_source = (ROOT / "titles/spyro3/core/spyro3_runtime.cpp").read_text(
            encoding="utf-8"
        )
        main = (ROOT / "game/core/main.cpp").read_text(encoding="utf-8")
        render_frame = (ROOT / "game/render/render_frame.cpp").read_text(
            encoding="utf-8"
        )
        hooks = (ROOT / "game/core/game_hooks.cpp").read_text(encoding="utf-8")

        self.assertRegex(
            lineage, r"class\s+SpyroRuntime\s*:\s*public\s+GameRuntime"
        )
        self.assertRegex(
            spyro1, r"class\s+Spyro1Runtime\s+final\s*:\s*public\s+spyro::SpyroRuntime"
        )
        self.assertRegex(
            spyro2, r"class\s+Spyro2Runtime\s+final\s*:\s*public\s+spyro::SpyroRuntime"
        )
        self.assertRegex(
            spyro3, r"class\s+Spyro3Runtime\s+final\s*:\s*public\s+spyro::SpyroRuntime"
        )
        self.assertIn("psxport_install_game(runtime);", main)
        self.assertIn("selectExecutableFile(path, spyro::executableCatalog())", main)
        self.assertLess(main.index("selectExecutableFile"), main.index("new Game"))
        self.assertLess(main.index("installSubstrate"), main.index("new Game"))
        self.assertNotIn("static spyro1::Spyro1Runtime runtime;", main)
        self.assertNotIn("GameConfig", spyro2_source)
        self.assertNotIn("compatibilityHooks", spyro2_source)
        self.assertNotIn("GameConfig", spyro3_source)
        self.assertNotIn("compatibilityHooks", spyro3_source)
        self.assertIn("guestVramIsPicture(const Game &game) const override", spyro1)
        spyro1_source = (ROOT / "titles/spyro1/core/spyro1_runtime.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("spyro_presentation_owner(game.core)", spyro1_source)
        self.assertIn("std::abort();", spyro2_source)
        self.assertIn("std::abort();", spyro3_source)
        self.assertNotRegex(
            hooks,
            r"\.(ctxCreate|ctxDestroy|bootInit|registerOverrides)\s*=",
        )
        self.assertEqual(
            set(re.findall(r"\.(\w+)\s*=", hooks)),
            {"fps60WorldPass", "fps60TemporalRotate", "selftestGame", "fps60ReadSceneCam"},
        )
        self.assertLess(
            render_frame.index("beginGuestFrame"), render_frame.index("referenceOtWalk();")
        )
        self.assertLess(
            render_frame.index("beginNativeFrame"), render_frame.index("mEnv = nativeFrameBegin")
        )


if __name__ == "__main__":
    unittest.main()
