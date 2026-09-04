#!/usr/bin/env python3
"""Resolve the shared scanner and apply Spyro's source-boundary manifest."""

from __future__ import annotations

import importlib.util
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path(__file__).with_name("source_policy.json")
PRODUCT_ROOTS = {"game", "titles"}
PRODUCT_BOUNDARY_PATTERNS = (
    re.compile(r"\bstd::cerr\b"),
    re.compile(r"\b(?:std::)?fprintf\s*\(\s*stderr\b"),
    re.compile(r"\b(?:std::)?fputs\s*\([^\n;]*\bstderr\b"),
    re.compile(r"\b(?:std::)?printf\s*\("),
    re.compile(r"\b(?:std::)?puts\s*\("),
    re.compile(r"\b(?:std::)?getenv\s*\("),
)
RETIRED_PRODUCT_VOCABULARY = (
    re.compile(r"(?i)\brecompiler\b"),
    re.compile(r"(?i)\brecomp\b"),
    re.compile(r"(?i)\btranscrib(?:e|ed|es|ing)?\b"),
    re.compile(r"\bgen_func_[0-9A-Fa-f]+\b"),
    re.compile(r"\b(?:ensure_recomp|recomp_seeds|static_dispatch)\b"),
)
SHARED_CANDIDATES = (
    Path(os.environ["RE_HARNESS_DIR"]) / "tools" / "source_boundary.py"
    if os.environ.get("RE_HARNESS_DIR")
    else Path("__missing_re_harness_override__"),
    ROOT.parents[1] / "shared" / "re-harness" / "tools" / "source_boundary.py",
    Path.home() / ".codex" / "bin" / "source_boundary.py",
    Path.home() / ".claude" / "bin" / "source_boundary.py",
)


def load_shared_scanner():
    for candidate in SHARED_CANDIDATES:
        if not candidate.is_file():
            continue
        spec = importlib.util.spec_from_file_location("re_harness_source_boundary", candidate)
        if spec is None or spec.loader is None:
            continue
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    attempted = "\n".join(f"  - {path}" for path in SHARED_CANDIDATES)
    raise SystemExit(f"source_policy: shared scanner not found; attempted:\n{attempted}")


def inspect_product_boundaries(paths, contents):
    """Reject product code that bypasses owners or revives retired execution vocabulary."""
    violations = []
    for path in paths:
        if not path.parts or path.parts[0] not in PRODUCT_ROOTS:
            continue
        text = contents.get(path, "")
        for pattern in PRODUCT_BOUNDARY_PATTERNS:
            if pattern.search(text):
                violations.append(f"{path}: product boundary bypass {pattern.pattern}")
        for pattern in RETIRED_PRODUCT_VOCABULARY:
            if pattern.search(text):
                violations.append(f"{path}: retired product vocabulary {pattern.pattern}")
    return violations


def product_boundary_selftest():
    clean = Path("game/clean.cpp")
    if inspect_product_boundaries([clean], {clean: 'lucent::info("boot", "ready");\n'}):
        return ["clean product fixture was rejected"]
    bad = {
        Path("game/cerr.cpp"): "std::cerr << value;\n",
        Path("game/fprintf.cpp"): 'fprintf(stderr, "bad");\n',
        Path("game/fputs.cpp"): "fputs(message, stderr);\n",
        Path("game/printf.cpp"): 'printf("bad");\n',
        Path("game/puts.cpp"): 'puts("bad");\n',
        Path("game/getenv.cpp"): 'getenv("MODE");\n',
        Path("game/recompiler.cpp"): "recompiler();\n",
        Path("game/recomp.cpp"): "recomp();\n",
        Path("game/transcribed.cpp"): "transcribed();\n",
        Path("game/generated.cpp"): "gen_func_80010000();\n",
        Path("game/seeds.cpp"): "recomp_seeds();\n",
        Path("game/dispatch.cpp"): "static_dispatch();\n",
    }
    violations = inspect_product_boundaries(list(bad), bad)
    expected = len(bad)
    if len(violations) != expected:
        return [
            f"product fixture scan found {len(violations)} violations; expected {expected}"
        ]
    print(f"PRODUCT SOURCE POLICY SELFTEST PASS: rejected {expected} negative fixtures")
    return []


def main():
    scanner = load_shared_scanner()
    if "--selftest" in sys.argv:
        result = scanner.main(ROOT, MANIFEST)
        failures = product_boundary_selftest()
        if failures:
            print("SOURCE BOUNDARY SELFTEST FAIL: " + "; ".join(failures))
            return 1
        return result
    result = scanner.main(ROOT, MANIFEST)
    if result:
        return result
    try:
        paths = scanner.tracked_paths(ROOT)
        manifest = scanner.load_manifest(MANIFEST)
        contents = scanner.read_live_sources(ROOT, paths, manifest)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"SOURCE BOUNDARY ERROR: cannot inspect product boundaries: {error}")
        return 2
    violations = inspect_product_boundaries(paths, contents)
    if violations:
        print("\n".join(violations))
        return 1
    product_paths = sum(
        bool(path.parts) and path.parts[0] in PRODUCT_ROOTS for path in paths
    )
    print(f"PRODUCT SOURCE POLICY PASS: checked {product_paths} product paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
