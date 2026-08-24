#!/usr/bin/env python3
"""Both-answer tests for serial-separated Spyro title provisioning."""

from __future__ import annotations

import hashlib
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import provision_title
import generate_title_catalog
import title_identity

SPYRO1 = provision_title.SPECS["spyro1"]
SPYRO2 = provision_title.SPECS["spyro2"]
SPYRO3 = provision_title.SPECS["spyro3"]


class ProvisionTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "scratch")
        self.root = pathlib.Path(self.temporary.name)
        self.disc = self.root / "disc.chd"
        self.disc.write_bytes(b"disc fixture")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def extractor(boot_target: str):
        def extract(_tool: pathlib.Path, _disc: pathlib.Path, output: pathlib.Path) -> None:
            output.mkdir(parents=True, exist_ok=True)
            (output / "SYSTEM.CNF").write_text(
                f"BOOT = cdrom:\\{boot_target};1\r\n", encoding="ascii"
            )
            (output / boot_target).write_bytes(b"identity fixture")

        return extract

    def test_cached_spyro1_cannot_satisfy_spyro2_request(self) -> None:
        spyro1_cache = self.root / "spyro1"
        spyro1_cache.mkdir()
        cached = spyro1_cache / SPYRO1.serial
        cached.write_bytes(b"cached Spyro 1")
        spyro2_cache = self.root / "spyro2"

        result = provision_title.provision(
            SPYRO2,
            self.disc,
            pathlib.Path("unused"),
            output_dir=spyro2_cache,
            extract=self.extractor(SPYRO2.serial),
            identity_check=lambda _: [],
        )

        self.assertEqual(result.name, SPYRO2.serial)
        self.assertEqual(result.read_bytes(), b"identity fixture")
        self.assertEqual(cached.read_bytes(), b"cached Spyro 1")

    def test_spyro3_has_a_distinct_serial_and_cache(self) -> None:
        result = provision_title.provision(
            SPYRO3,
            self.disc,
            pathlib.Path("unused"),
            output_dir=self.root / "spyro3",
            extract=self.extractor(SPYRO3.serial),
            identity_check=lambda _: [],
        )
        self.assertEqual(result.name, "SCUS_944.67")
        self.assertNotEqual(SPYRO3.cache_dir, SPYRO1.cache_dir)
        self.assertNotEqual(SPYRO3.cache_dir, SPYRO2.cache_dir)

    def test_selected_spyro1_disc_is_refused_for_spyro2_even_with_cache(self) -> None:
        output = self.root / "spyro2"
        output.mkdir()
        stale = output / SPYRO2.serial
        stale.write_bytes(b"old Spyro 2 cache")

        with self.assertRaisesRegex(
            provision_title.Refused, rf"{SPYRO1.serial}.*{SPYRO2.serial}"
        ):
            provision_title.provision(
                SPYRO2,
                self.disc,
                pathlib.Path("unused"),
                output_dir=output,
                extract=self.extractor(SPYRO1.serial),
                identity_check=lambda _: [],
            )

        self.assertEqual(stale.read_bytes(), b"old Spyro 2 cache")
        self.assertFalse((output / "SYSTEM.CNF").exists())

    def test_identity_disagreement_does_not_publish(self) -> None:
        output = self.root / "identity-failure"
        with self.assertRaisesRegex(provision_title.Refused, "1 tracked fact"):
            provision_title.provision(
                SPYRO1,
                self.disc,
                pathlib.Path("unused"),
                output_dir=output,
                extract=self.extractor(SPYRO1.serial),
                identity_check=lambda _: ["sha256 mismatch"],
            )
        self.assertFalse((output / SPYRO1.serial).exists())
        self.assertFalse((output / "SYSTEM.CNF").exists())


class ResolverTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "scratch")
        self.root = pathlib.Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_title_specific_environment_keys_do_not_cross_select(self) -> None:
        disc = self.root / "spyro1.chd"
        disc.write_bytes(b"fixture")
        with self.assertRaisesRegex(provision_title.Refused, "no disc image"):
            provision_title.resolve_disc(
                SPYRO2,
                None,
                root=self.root / "empty",
                environ={"PSXPORT_SPYRO1_DISC": str(disc)},
            )

    def test_multiple_dropins_are_refused_as_ambiguous(self) -> None:
        (self.root / "one.chd").write_bytes(b"one")
        (self.root / "two.chd").write_bytes(b"two")
        with self.assertRaisesRegex(provision_title.Refused, "ambiguous"):
            provision_title.resolve_disc(SPYRO1, None, root=self.root, environ={})


class IdentityTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "scratch")
        self.root = pathlib.Path(self.temporary.name)
        data = bytearray(0x900)
        data[:8] = b"PS-X EXE"
        struct.pack_into("<4I", data, 0x10, 0x80010020, 0, 0x80010000, 0x100)
        struct.pack_into("<2I", data, 0x30, 0x801FFFF0, 0)
        data[0x800 : 0x806] = b"marker"
        self.executable = self.root / "SCUS_TEST.00"
        self.executable.write_bytes(data)
        self.manifest = {
            "title": "identity fixture",
            "region": "test",
            "serial": self.executable.name,
            "executable": self.executable.name,
            "file_size": len(data),
            "sha1": hashlib.sha1(data).hexdigest(),
            "sha256": hashlib.sha256(data).hexdigest(),
            "header": {
                "entry": "0x80010020",
                "gp": "0x0",
                "text_address": "0x80010000",
                "text_size": "0x100",
                "stack_address": "0x801FFFF0",
                "stack_offset": "0x0",
            },
            "region_markers": ["marker"],
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_match_and_mutation_produce_both_answers(self) -> None:
        self.assertEqual(title_identity.check(self.manifest, self.executable, verbose=False), [])
        data = bytearray(self.executable.read_bytes())
        data[-1] ^= 1
        self.executable.write_bytes(data)
        failures = title_identity.check(self.manifest, self.executable, verbose=False)
        self.assertEqual(len(failures), 2)
        self.assertTrue(any(failure.startswith("sha1:") for failure in failures))
        self.assertTrue(any(failure.startswith("sha256:") for failure in failures))

    def test_malformed_executable_is_refused(self) -> None:
        self.executable.write_bytes(b"not a PS-X EXE")
        with self.assertRaisesRegex(title_identity.Refused, "not a valid PS-X EXE"):
            title_identity.check(self.manifest, self.executable, verbose=False)


class CatalogGenerationTest(unittest.TestCase):
    def test_catalog_is_generated_from_all_three_manifests(self) -> None:
        catalog = generate_title_catalog.render()
        for slug, enum_name in generate_title_catalog.TITLE_ENUMS:
            manifest = title_identity.load_manifest(slug)
            self.assertIn(f"SpyroTitle::{enum_name}", catalog)
            self.assertIn(str(manifest["serial"]), catalog)
            self.assertIn(str(manifest["sha256"]), catalog)
        self.assertNotIn("GameConfig", catalog)


if __name__ == "__main__":
    unittest.main()
