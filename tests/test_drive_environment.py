#!/usr/bin/env python3
"""The agent launch environment starts every run without a memory card (docs/issues/0169).

The card is an input the guest reads and a file the run writes. With no PSXPORT_CARD the product opens one
shared scratch/saves/card.mcr, so each run started from whatever the previous run left: Spyro 3's title
route reached gameplay at field 4890 on one card and 4740 on the next. These cases pin that the driver's
environment names a private card, removes any image left there, and that the opt-out leaves the operator's
own card alone.
"""

import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import drive  # noqa: E402

CARD_ENV = "PSXPORT_CARD"


class DriveEnvironmentCard(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.directory, ignore_errors=True)

    def test_the_route_card_is_private_and_is_not_the_shared_default(self) -> None:
        with mock.patch.dict(os.environ):
            os.environ.pop(CARD_ENV, None)
            environment = drive.environment(None, card=drive.ROUTE_CARD)
        self.assertEqual(environment[CARD_ENV], str(drive.ROUTE_CARD))
        self.assertNotEqual(drive.ROUTE_CARD, drive.ROOT / "scratch" / "saves" / "card.mcr")

    def test_the_title_routes_launch_on_the_route_card(self) -> None:
        source = (ROOT / "tools" / "title_route.py").read_text()
        self.assertIn("drive.environment(None, card=drive.ROUTE_CARD)", source)

    def test_a_card_left_by_the_previous_run_is_removed_before_launch(self) -> None:
        card = self.directory / "route.mcr"
        card.write_bytes(b"the previous run's save")
        environment = drive.environment(None, card=card)
        self.assertEqual(environment[CARD_ENV], str(card))
        self.assertFalse(card.exists())

    def test_an_inherited_card_variable_does_not_override_the_route_card(self) -> None:
        card = self.directory / "route.mcr"
        with mock.patch.dict(os.environ, {CARD_ENV: str(self.directory / "operator.mcr")}):
            environment = drive.environment(None, card=card)
        self.assertEqual(environment[CARD_ENV], str(card))

    def test_with_no_card_the_operators_card_stays_in_force_and_on_disk(self) -> None:
        operator = self.directory / "operator.mcr"
        operator.write_bytes(b"keep me")
        with mock.patch.dict(os.environ, {CARD_ENV: str(operator)}):
            environment = drive.environment(None)
        self.assertEqual(environment[CARD_ENV], str(operator))
        self.assertEqual(operator.read_bytes(), b"keep me")


if __name__ == "__main__":
    unittest.main()
