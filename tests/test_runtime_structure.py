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
        spyro2_frame = (
            ROOT / "titles/spyro2/core/spyro2_frame_driver.cpp"
        ).read_text(encoding="utf-8")
        spyro2_display = (
            ROOT / "titles/spyro2/core/spyro2_display_bootstrap.cpp"
        ).read_text(encoding="utf-8")
        spyro2_gpu_sync = (
            ROOT / "titles/spyro2/core/spyro2_gpu_sync.cpp"
        ).read_text(encoding="utf-8")
        spyro2_register = (
            ROOT / "titles/spyro2/core/spyro2_recomp_register.cpp"
        ).read_text(encoding="utf-8")
        spyro2_main = (ROOT / "titles/spyro2/core/main.cpp").read_text(
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
        self.assertIn("RenderCapabilities::interpolatedNative()", spyro1)
        self.assertIn(".defaultPath = RenderPath::Gte", lineage)
        self.assertIn(".nativeRenderPath = false", lineage)
        self.assertIn(".temporalInterpolation = false", lineage)
        spyro1_source = (ROOT / "titles/spyro1/core/spyro1_runtime.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("std::make_unique<Fps60>(game)", spyro1_source)
        self.assertIn("spyro_register_wide_clip();", spyro1_source)
        frame_driver = (
            ROOT / "titles/spyro1/core/spyro1_frame_driver.cpp"
        ).read_text(encoding="utf-8")
        field_scheduler = (
            ROOT / "titles/spyro1/core/spyro1_field_scheduler.cpp"
        ).read_text(encoding="utf-8")
        boot_sequence = (
            ROOT / "titles/spyro1/core/spyro1_boot_sequence.cpp"
        ).read_text(encoding="utf-8")
        vsync_registration = (ROOT / "game/core/vsync.cpp").read_text(
            encoding="utf-8"
        )
        game_config = (ROOT / "game/core/game_config.cpp").read_text(
            encoding="utf-8"
        )
        cmake_sources = (ROOT / "cmake/spyro_port.cmake").read_text(encoding="utf-8")
        self.assertIn("spyro_presentation_owner(game.core)", spyro1_source)
        self.assertIn("createFrameDriver(Game &game) override", spyro1)
        self.assertIn("std::make_unique<Spyro1FrameDriver>(game)", spyro1_source)
        self.assertIn("frameDriver(core).initialize(core)", spyro1_source)
        self.assertNotIn("for (", spyro1_source)
        self.assertIn("void Spyro1FrameDriver::stepFrame", frame_driver)
        self.assertNotIn("Spyro1FrameDriver::run(Core", frame_driver)
        self.assertIn("boot_.step(core)", frame_driver)
        self.assertNotIn("rc0(&core, 0x800127C0", frame_driver)
        self.assertEqual(frame_driver.count("renderer_->drawFrame();"), 1)
        self.assertIn(".vsyncTrap = 0x8005DBC4u", game_config)
        self.assertIn(".gpuTimeoutArm = 0x80062090u", game_config)
        self.assertIn(".gpuTimeoutCheck = 0x800620C4u", game_config)
        self.assertIn(".gpuTimeoutDeadlineVar = 0x80074B7Cu", game_config)
        self.assertIn(".gpuTimeoutFlagVar = 0x80074B80u", game_config)
        self.assertNotIn("platform_hle.register_", vsync_registration)
        self.assertNotIn("legacyVblankWait", vsync_registration)
        self.assertNotIn("0x8005DBC4u", boot_sequence)
        self.assertIn("fields_.bootPresentationSkipPressed()", boot_sequence)
        self.assertIn("leaveFirstPresentationHold(core)", boot_sequence)
        self.assertIn("leaveSecondPresentationHold()", boot_sequence)
        self.assertIn("pressedButton(kPadStart | kPadCross)", field_scheduler)
        for service in ("pad.serviceFrame", "spu_audio.frame", "gpu_present"):
            self.assertNotIn(service, vsync_registration)
            self.assertIn(service, field_scheduler)
        self.assertNotIn("game/core/frame_loop.cpp", cmake_sources)
        self.assertIn("return true;", spyro2_source)
        self.assertIn("std::make_unique<Spyro2FrameDriver>(game)", spyro2_source)
        self.assertIn("kStaticConstructors", spyro2_frame)
        self.assertIn("kBootPrefixFirstLeaf", spyro2_frame)
        self.assertIn("kDisplayBootstrap", spyro2_frame)
        self.assertNotIn("call(core, kDisplayBootstrap", spyro2_frame)
        self.assertNotIn("0x80058EDC", spyro2_frame)
        self.assertNotIn("0x80058EDC", spyro2_display)
        self.assertNotIn("call(core, 0x8004C484", spyro2_display)
        self.assertEqual(spyro2_display.count("game_.presentation.commit(&core, 1);"), 1)
        self.assertIn("kClearFrameBytes", spyro2_display)
        self.assertIn("phase_ = Phase::Complete", spyro2_display)
        self.assertNotIn("kDrawSync", spyro2_display)
        self.assertEqual(spyro2_display.count("completeDrawSync(core);"), 2)
        self.assertIn("core.r[2] = 0u;", spyro2_gpu_sync)
        self.assertIn("gen_func_800557E4", spyro2_register)
        self.assertIn("gen_func_80057880", spyro2_register)
        self.assertIn("gen_func_800578B4", spyro2_register)
        self.assertIn("shard_set_override", spyro2_register)
        self.assertIn("std::make_unique<Game>()", spyro2_main)
        self.assertIn("dc_step_frame(&core, frame);", spyro2_main)
        self.assertNotIn("GameConfig", spyro2_main)
        self.assertIn("std::abort();", spyro3_source)
        self.assertNotRegex(
            hooks,
            r"\.(ctxCreate|ctxDestroy|bootInit|registerOverrides)\s*=",
        )
        self.assertEqual(
            set(re.findall(r"\.(\w+)\s*=", hooks)),
            {
                "replCommand",
                "fps60WorldPass",
                "fps60TemporalRotate",
                "selftestGame",
                "fps60ReadSceneCam",
            },
        )
        self.assertLess(
            render_frame.index("beginGuestFrame"), render_frame.index("referenceOtWalk();")
        )
        self.assertLess(
            render_frame.index("beginNativeFrame"), render_frame.index("mEnv = nativeFrameBegin")
        )


if __name__ == "__main__":
    unittest.main()
