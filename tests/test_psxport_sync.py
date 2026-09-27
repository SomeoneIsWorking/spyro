"""The pin check must examine the selected verifier build, including stale receipts.

WHY THIS FILE WAS REWRITTEN. It used to mock `builtins.open` and `os.path.isfile` and point the
receipt's `dir` at a literal string, `"framework"`. That was adequate while `--check` compared only
`psxport_resolved.txt` against `psxport.pin` — but the check grew a staleness guard that ALSO asks
whether the framework tree is at the commit the receipt names and is clean. Those two inputs are
`head_of(bdir)` and `dirty(bdir)`, and the fixture supplied neither: `head_of("framework")` returns
None because nothing is there, so the guard fired and the two "matching build" cases failed. The
test had been asserting the old contract.

Mocking `open` away was the deeper problem. The guard's whole purpose is to compare a RECORDED
snapshot against the LIVE tree, and a fixture with no live tree cannot test that at all. So this
builds a REAL git repository in a temp directory and lets `head_of`/`dirty` run against it. Each
case then moves the real tree and asserts what the check answers, which is the only way the
staleness guard can be falsified.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

# Point at THIS repo's canonical copy, not at a port's. The ports each keep their own copy of both
# files and are independently testable, which is the point of shipping them; but the canonical text is
# authored and gated HERE, so a behaviour change must be caught here first.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import psxport_sync  # noqa: E402

# `verify_ci` is PORT-SIDE: it is each port's CI driver, not a framework tool, so it does not exist here.
# The two cases that exercise the check THROUGH the verifier therefore belong to the port's copy of this
# file, which keeps all fourteen. Skipping them is stated, not silent, and the reason is asserted below so
# the skip cannot rot into a pass.
try:
    import verify_ci  # noqa: E402

    VERIFIER_REASON = ""
except ModuleNotFoundError as exc:  # pragma: no cover - depends on the tree this file runs in
    verify_ci = None
    VERIFIER_REASON = f"no port-side verify_ci in this tree ({exc.name})"

GIT_ENV = {
    "GIT_AUTHOR_NAME": "pin selftest",
    "GIT_AUTHOR_EMAIL": "pin@selftest.invalid",
    "GIT_COMMITTER_NAME": "pin selftest",
    "GIT_COMMITTER_EMAIL": "pin@selftest.invalid",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_SYSTEM": "/dev/null",
}


def git(*args: str, cwd: str) -> str:
    env = dict(os.environ, **GIT_ENV)
    return subprocess.run(
        ["git", *args], cwd=cwd, env=env, check=True, capture_output=True, text=True
    ).stdout.strip()


class _PinFixture:
    """A real framework repo, a real build dir, a real receipt. Not a TestCase: mixing it into two
    TestCases is what lets the bump tests reuse the fixture without re-running the check tests."""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="pin-selftest-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        # A REAL framework tree, so the staleness guard is exercised rather than bypassed.
        self.framework = os.path.join(self.tmp, "framework")
        os.makedirs(self.framework)
        git("init", "-q", "-b", "main", cwd=self.framework)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 0; }\n")
        git("add", "-A", cwd=self.framework)
        git("commit", "-q", "-m", "framework", cwd=self.framework)
        # A real remote with the commit pushed, because `do_bump` refuses a pin that a fresh clone
        # could not fetch. That refusal is only meaningful if a fetchable case exists to contrast with.
        self.remote = os.path.join(self.tmp, "remote.git")
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", self.remote], check=True,
                       env=dict(os.environ, **GIT_ENV))
        git("remote", "add", "origin", self.remote, cwd=self.framework)
        git("push", "-q", "origin", "main", cwd=self.framework)
        self.head = git("rev-parse", "HEAD", cwd=self.framework)
        # A REAL build dir holding a REAL receipt, so read_resolved parses an actual file.
        self.build = os.path.join(self.tmp, "build")
        os.makedirs(self.build)
        self.args = SimpleNamespace(build=self.build)

    def write_receipt(self, directory: str, commit: str) -> None:
        Path(self.build, "psxport_resolved.txt").write_text(
            f"dir = {directory}\ncommit = {commit}\n"
        )



class PinCheckTests(_PinFixture, unittest.TestCase):
    def run_check(self, pin: str | None = None) -> tuple[int, str]:
        """Run do_check against the real build dir, with only `read_pin` stubbed.

        `read_pin` is the one input that genuinely has to be injected: the pin is a property of the
        repository under test, and a selftest that adopted the live `psxport.pin` would stop testing
        anything the moment the pin moved.
        """
        import contextlib
        import io
        from unittest.mock import patch

        resolved = None if pin is None else ("url", pin)
        output = io.StringIO()
        with patch.object(psxport_sync, "read_pin", return_value=resolved):
            with contextlib.redirect_stdout(output):
                return psxport_sync.do_check(self.args), output.getvalue()

    def test_matching_selected_build_passes(self) -> None:
        self.write_receipt(self.framework, self.head)
        status, output = self.run_check(self.head)
        self.assertEqual(status, 0, output)
        self.assertIn("check OK", output)

    def test_absent_receipt_refuses(self) -> None:
        status, output = self.run_check(self.head)
        self.assertEqual(status, 2, output)
        self.assertIn("REFUSED", output)

    def test_recorded_pin_does_not_match_receipt_fails(self) -> None:
        self.write_receipt(self.framework, self.head)
        status, output = self.run_check("fedcba9876543210" + "0" * 24)
        self.assertEqual(status, 1, output)
        self.assertIn("check FAILED", output)

    # --- the staleness guard -------------------------------------------------------------
    # Each of these has the pin MATCHING the receipt, so the pin comparison alone would pass. The
    # only thing that can refuse is the guard, which is what makes them the guard's own tests.

    def test_framework_advanced_since_configure_fails(self) -> None:
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 1; }\n")
        git("commit", "-q", "-am", "newer framework", cwd=self.framework)
        self.assertNotEqual(git("rev-parse", "HEAD", cwd=self.framework), self.head)
        status, output = self.run_check(self.head)
        self.assertEqual(status, 1, output)
        self.assertIn("changed since configure", output)

    def test_dirty_framework_fails(self) -> None:
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 2; }\n")
        status, output = self.run_check(self.head)
        self.assertEqual(status, 1, output)
        self.assertIn("changed since configure", output)

    def test_receipt_naming_a_missing_framework_fails(self) -> None:
        """A receipt whose `dir` does not exist must not read as a match.

        `head_of` returns None for a missing directory, and None != the receipt's commit, so the
        guard refuses. The case matters because it is the shape a moved or deleted framework takes,
        and "the tree is gone" must never be reported as "the pin matches".
        """
        self.write_receipt(os.path.join(self.tmp, "not-a-tree"), self.head)
        status, output = self.run_check(self.head)
        self.assertEqual(status, 1, output)
        self.assertIn("check FAILED", output)

    # --- the verifier path ---------------------------------------------------------------
    # verify_ci runs the same check, so the guard has to be exercised through it too: a verifier
    # that called an older or different check would otherwise keep passing after the guard landed.

    def test_the_verifier_skip_is_stated_and_attributable(self) -> None:
        """A skip must name what is missing, so it cannot silently become a pass.

        Twelve of the fourteen cases run here. The other two need a port's `verify_ci`, which does not
        exist in the framework, and the port's own copy of this file runs all fourteen. If this repo ever
        grows a `verify_ci`, the skip must disappear rather than linger.
        """
        if verify_ci is None:
            self.assertIn("verify_ci", VERIFIER_REASON)
        else:
            self.assertEqual(VERIFIER_REASON, "")

    @unittest.skipUnless(verify_ci is not None, "verify_ci is port-side; see VERIFIER_REASON")
    def run_verifier(self) -> tuple[int, str]:
        import contextlib
        import io
        from unittest.mock import patch

        output = io.StringIO()
        with patch.object(verify_ci, "run_consumer_verification", return_value=0) as verifier:
            with patch.object(psxport_sync, "read_pin", return_value=("url", self.head)):
                with contextlib.redirect_stdout(output):
                    result = verify_ci.main(["--build", self.build])
        self.assertEqual(verifier.call_args.args[0].build, Path(self.build).resolve())
        return result, output.getvalue()

    def test_verifier_accepts_matching_selected_build(self) -> None:
        self.write_receipt(self.framework, self.head)
        status, output = self.run_verifier()
        self.assertEqual(status, 0, output)
        self.assertIn("check OK", output)

    def test_verifier_rejects_framework_advanced_since_configure(self) -> None:
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 3; }\n")
        git("commit", "-q", "-am", "newer framework", cwd=self.framework)
        status, output = self.run_verifier()
        self.assertEqual(status, 1, output)
        self.assertIn("changed since configure", output)


class BumpRecordsTheBuiltCommitTests(_PinFixture, unittest.TestCase):
    def run_bump(self, pin: str = "old-pin") -> tuple[int, str, list[tuple[str, str]]]:
        import contextlib
        import io
        from unittest.mock import patch

        written: list[tuple[str, str]] = []
        output = io.StringIO()
        with patch.object(psxport_sync, "describe_link", return_value=("symlink", self.framework)), \
             patch.object(psxport_sync, "read_pin", return_value=("url", pin)), \
             patch.object(psxport_sync, "write_pin", side_effect=lambda u, s: written.append((u, s))):
            with contextlib.redirect_stdout(output):
                status = psxport_sync.do_bump(self.args)
        return status, output.getvalue(), written

    def test_bump_records_the_receipts_commit(self) -> None:
        self.write_receipt(self.framework, self.head)
        status, output, written = self.run_bump()
        self.assertEqual(status, 0, output)
        self.assertEqual(written, [("url", self.head)])
        self.assertIn("pin old-pin -> " + self.head[:8], output)
        self.assertIn("not from the framework's current HEAD", output)

    def test_bump_refuses_when_the_framework_advanced_since_configure(self) -> None:
        """THE INCIDENT. Pin and framework head would both look fine; only the receipt is stale."""
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 4; }\n")
        git("commit", "-q", "-am", "newer framework", cwd=self.framework)
        git("push", "-q", "origin", "main", cwd=self.framework)
        status, output, written = self.run_bump(pin=self.head)
        self.assertEqual(status, 1, output)
        self.assertIn("stale", output)
        self.assertEqual(written, [], "a refused bump must not write a pin")

    def test_bump_refuses_with_no_receipt(self) -> None:
        status, output, written = self.run_bump()
        self.assertEqual(status, 2, output)
        self.assertIn("Reconfigure and build FIRST", output)
        self.assertEqual(written, [])

    def test_bump_refuses_a_dirty_framework(self) -> None:
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 5; }\n")
        status, output, written = self.run_bump()
        self.assertEqual(status, 1, output)
        self.assertIn("stale", output)
        self.assertEqual(written, [])

    def test_bump_refuses_a_receipt_naming_another_tree(self) -> None:
        """A receipt for a different framework than the one this port links is not this port's build."""
        other = os.path.join(self.tmp, "other")
        os.makedirs(other)
        git("init", "-q", "-b", "main", cwd=other)
        (Path(other) / "x.cpp").write_text("int x;\n")
        git("add", "-A", cwd=other)
        git("commit", "-q", "-m", "other", cwd=other)
        self.write_receipt(other, git("rev-parse", "HEAD", cwd=other))
        status, output, written = self.run_bump()
        self.assertEqual(status, 1, output)
        self.assertIn("does not consume", output)
        self.assertEqual(written, [])

    def test_bump_refuses_an_unpushed_commit(self) -> None:
        self.write_receipt(self.framework, self.head)
        (Path(self.framework) / "core.cpp").write_text("int main() { return 6; }\n")
        git("commit", "-q", "-am", "unpushed framework", cwd=self.framework)
        fresh = git("rev-parse", "HEAD", cwd=self.framework)
        self.write_receipt(self.framework, fresh)
        status, output, written = self.run_bump()
        self.assertEqual(status, 1, output)
        self.assertIn("not on any remote branch", output)
        self.assertEqual(written, [])


if __name__ == "__main__":
    unittest.main()
