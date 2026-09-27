#!/usr/bin/env python3
"""psxport_sync.py — THE CANONICAL COPY. Do not edit this in a port; edit it here.

WHY A CANONICAL SOURCE EXISTS
-----------------------------
Every port ships its own copy of this file, because a port must build from a bare clone of ITSELF and
cannot depend on the workspace. That is deliberate and correct. The cost is that the copies drift, and
the drift was not hypothetical:

  - MEASURED 2026-09-27: the ten copies had TEN DISTINCT HASHES and 298–322 lines each.
  - The staleness guard was MISSING FROM SEVEN OF THE TEN, and where it was missing the check answered
    `check OK` on input a guarded copy refused. Same input, opposite answers, and the wrong one is a pass.
  - Only ONE of the ten had a `--build` flag, so the live pin check that actually calls this function
    could not be registered in the other nine.
  - Only ONE had the `do_bump` fix, so the other nine could still record a framework commit the tree was
    never built against — which is the original incident this whole mechanism exists to prevent.

The obligation to keep the copies in step was real, and the check for that obligation was ITSELF
duplicated, which is why the guard could go missing from seven of them unnoticed. This file plus
`tools/check_port_pin_tools.py` is that check, made mechanical: it compares every port's copy against
this one and fails on drift, and `--install` makes the copies identical again.

The check is a registered gate, not a habit. See `tools/check_port_pin_tools.py --selftest`.

`REPO` is derived from this file's own location, so the text runs unchanged from any port's `tools/`.
The canonical copy in psxport is never executed as a port tool; it is only compared and copied.
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = os.path.join(REPO, "external", "psxport")
PIN = os.path.join(REPO, "psxport.pin")
CANONICAL_VERIFY_BUILD = os.path.join(REPO, "build", "ci")
DEFAULT_URL = "https://github.com/SomeoneIsWorking/psxport.git"

# Where a shared clone lives, in preference order. $PSX wins so a differently-laid-out workspace works.
def shared_candidates():
    out = []
    psx = os.environ.get("PSX")
    if psx:
        out.append(os.path.join(psx, "psxport"))
    out.append(os.path.abspath(os.path.join(REPO, "..", "psxport")))
    return out


def git(args, cwd, check=False):
    p = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed in {cwd}: {p.stderr.strip()}")
    return p.stdout.strip(), p.returncode


def is_psxport_checkout(path):
    return os.path.isfile(os.path.join(path, "cmake", "psxport.cmake"))


def read_pin():
    """Returns (url, commit) or (None, None). A malformed pin is a refusal, never a silent default."""
    if not os.path.isfile(PIN):
        return None, None
    url, commit = DEFAULT_URL, None
    for line in open(PIN):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"(\w+)\s*=\s*(\S+)", line)
        if not m:
            continue
        if m.group(1) == "url":
            url = m.group(2)
        elif m.group(1) == "commit":
            commit = m.group(2)
    return url, commit


def write_pin(url, commit):
    with open(PIN, "w") as fh:
        fh.write(
            "# psxport framework pin — the commit this game was built and VERIFIED against.\n"
            "# Managed by tools/psxport_sync.py (--bump to record the framework you are building\n"
            "# against now). This is provenance and the fresh-clone fallback; locally the build runs\n"
            "# off the shared framework clone via the external/psxport symlink. Ports are deliberately\n"
            "# not all on framework HEAD — see the module docstring for why.\n"
            f"url = {url}\n"
            f"commit = {commit}\n"
        )


def describe_link():
    """What external/psxport currently IS. Never guesses — 'missing' is a real answer."""
    if os.path.islink(LINK):
        return "symlink", os.path.realpath(LINK)
    if os.path.isdir(LINK):
        return ("clone" if os.path.isdir(os.path.join(LINK, ".git")) or
                os.path.isfile(os.path.join(LINK, ".git")) else "plain-dir"), LINK
    return "missing", LINK


def head_of(path):
    if not os.path.isdir(path):
        return None
    sha, rc = git(["rev-parse", "HEAD"], path)
    return sha if rc == 0 else None


def dirty(path):
    out, rc = git(["status", "--porcelain"], path)
    return bool(out) if rc == 0 else False


def report(args):
    kind, target = describe_link()
    _, pin = read_pin()
    sha = head_of(target) if kind in ("symlink", "clone") else None
    print(f"[psxport] external/psxport : {kind}" + (f" -> {target}" if kind == "symlink" else ""))
    print(f"[psxport] framework HEAD   : {sha or '(none)'}"
          + ("  +dirty" if sha and dirty(target) else ""))
    print(f"[psxport] recorded pin     : {pin or '(no psxport.pin)'}")
    built = read_resolved(args.build)
    if built:
        print(f"[psxport] last build used  : {built[1]}  (from {built[0]})")
    if sha and pin:
        if sha == pin:
            print("[psxport] IN SYNC — the checkout you build from is the commit this repo records.")
        else:
            ahead, _ = git(["rev-list", "--count", f"{pin}..{sha}"], target)
            print(f"[psxport] DRIFT — the checkout is {ahead or '?'} commit(s) off the recorded pin. "
                  f"That is normal WHILE doing framework work; record it before you land game code "
                  f"that needs it:  python3 tools/psxport_sync.py --bump")
    return 0


def read_resolved(build):
    """(dir, sha) the selected CMake configure resolved, or None. Written by CMakeLists."""
    receipt = os.path.join(build, "psxport_resolved.txt")
    if not os.path.isfile(receipt):
        return None
    d = s = None
    # Closed explicitly. A bare `for line in open(...)` leaves the handle to the garbage collector,
    # which is invisible in normal use but shows up as a ResourceWarning the moment a test reads
    # real receipts repeatedly — the selftest does, and it found this.
    with open(receipt, encoding="utf-8") as handle:
        lines = handle.readlines()
    for line in lines:
        k, _, v = line.partition("=")
        if k.strip() == "dir":
            d = v.strip()
        elif k.strip() == "commit":
            s = v.strip()
    return (d, s) if s else None


def do_link(args):
    for cand in shared_candidates():
        if is_psxport_checkout(cand):
            kind, target = describe_link()
            if kind == "clone" and not args.force:
                print(f"[psxport] REFUSED: external/psxport is a real clone. Replacing it would discard "
                      f"anything unpushed in it. Inspect it, then re-run with --force.")
                return 2
            if kind in ("clone", "plain-dir"):
                subprocess.run(["rm", "-rf", LINK], check=True)
            elif kind == "symlink":
                os.unlink(LINK)
            os.makedirs(os.path.dirname(LINK), exist_ok=True)
            os.symlink(os.path.relpath(cand, os.path.dirname(LINK)), LINK)
            print(f"[psxport] external/psxport -> {cand}  (shared clone; framework edits are live here)")
            return 0
    print("[psxport] no shared framework clone found. Looked in: "
          + ", ".join(shared_candidates())
          + "\n[psxport] use --clone to fetch a private one at the pin instead.")
    return 2


def do_clone(args):
    url, pin = read_pin()
    if not pin:
        print("[psxport] REFUSED: no usable psxport.pin, so there is no commit to clone to.")
        return 2
    kind, _ = describe_link()
    if kind == "symlink":
        os.unlink(LINK)
    if not os.path.isdir(LINK):
        os.makedirs(os.path.dirname(LINK), exist_ok=True)
        print(f"[psxport] cloning {url} -> external/psxport")
        _, rc = git(["clone", url, LINK], REPO)
        if rc != 0:
            print("[psxport] REFUSED: clone failed.")
            return 2
    git(["fetch", "origin"], LINK)
    _, rc = git(["checkout", pin], LINK)
    if rc != 0:
        print(f"[psxport] REFUSED: pin {pin} is not reachable in {url}. A fresh clone of this repo "
              f"CANNOT build. Push the framework commit, then re-pin.")
        return 1
    # Nested vendor submodules are psxport's own; init them non-recursively, because beetle carries a
    # URL-less nested gitlink that makes --recursive fail outright.
    git(["submodule", "update", "--init", "vendor/beetle-psx", "vendor/lucent"], LINK)
    git(["submodule", "update", "--init", "deps/libchdr"], os.path.join(LINK, "vendor", "beetle-psx"))
    print(f"[psxport] external/psxport cloned at pin {pin}")
    return 0


def do_auto(args):
    for cand in shared_candidates():
        if is_psxport_checkout(cand):
            kind, target = describe_link()
            if kind == "symlink" and os.path.realpath(target) == os.path.realpath(cand):
                return 0            # already pointed at the shared clone
            return do_link(args)
    kind, _ = describe_link()
    if kind == "clone" and is_psxport_checkout(LINK):
        return 0                    # a private clone is already in place
    return do_clone(args)


def do_bump(args):
    """Record the framework commit THIS BUILD resolved -- not the framework's current HEAD.

    WHY THIS IS NOT `head_of(target)`. It used to be. Recording HEAD means the pin names whatever the
    framework is at when you run the bump, which is unrelated to what this tree was compiled and tested
    against, and it makes the documented order `reconfigure -> build -> test -> --bump` a convention that
    nothing enforces: `--bump` alone, with no build at all, would record a commit this tree has never
    seen. That is not hypothetical. This repo shipped built against psxport `25dd7826` while recording
    `a1c53d7c`, so a bare clone named a framework whose `GameHooks` lacked a field the game used, and
    nothing noticed because a submodule working tree and its recorded gitlink drift silently. The pin
    existed to make that failure loud, and the bump was the one step that could re-create it.

    So a bump reads the same receipt, applies the same staleness guard, and selects the same build as
    `--check`, which makes the two agree by construction rather than by two people remembering an order.
    """
    kind, target = describe_link()
    built = read_resolved(args.build)
    if not built:
        print(f"[psxport] REFUSED: no usable psxport_resolved.txt in {args.build}; nothing was configured "
              f"there, so there is nothing to record. Reconfigure and build FIRST, then bump -- a pin "
              f"records a verification, and no build is no verification.")
        return 2
    bdir, bsha = built
    # The build must have resolved the tree this repo LINKS, or the receipt describes a framework this
    # port is not actually consuming.
    if kind in ("symlink", "clone") and os.path.realpath(bdir) != os.path.realpath(target):
        print(f"[psxport] REFUSED: {args.build} was configured against {bdir}, but external/psxport "
              f"points at {target}. Bumping would record a framework this port does not consume.")
        return 1
    # The same guard --check applies: a receipt that is already stale is not a verification.
    current = head_of(bdir)
    if current != bsha or dirty(bdir):
        print(f"[psxport] REFUSED: {args.build}'s receipt is stale -- framework {bdir} is dirty or "
              f"changed since configure (configured {bsha}, current {current}). Reconfigure, rebuild "
              f"and retest, then bump.")
        return 1
    url, old = read_pin()
    remote_has, rc = git(["branch", "-r", "--contains", bsha], bdir)
    if rc != 0 or not remote_has.strip():
        print(f"[psxport] REFUSED: {bsha[:8]} is not on any remote branch. Recording it would leave a "
              f"pin that a fresh clone cannot fetch -- which is exactly how this repo shipped a tree "
              f"that did not build standalone. Push the framework first.")
        return 1
    write_pin(url or DEFAULT_URL, bsha)
    print(f"[psxport] pin {(old or '(none)')[:8]} -> {bsha[:8]}")
    print(f"[psxport]   recorded from {args.build}'s receipt -- the commit that build resolved -- not "
          f"from the framework's current HEAD.")
    return 0


def do_check(args):
    """The precommit check: what you BUILT against must be what this repo RECORDS."""
    return check_build_pin(args.build)


def check_build_pin(build):
    """Refuse unless this exact CMake build's framework receipt matches the recorded pin."""
    _, pin = read_pin()
    if not pin:
        print("[psxport] REFUSED: no psxport.pin — this check asserted NOTHING.")
        return 2
    built = read_resolved(build)
    if not built:
        print(f"[psxport] REFUSED: no usable psxport_resolved.txt in {build}; "
              f"nothing can be compared with pin {pin[:8]}.")
        return 2
    bdir, bsha = built
    # THE STALENESS GUARD, and it is the whole point of this check. `bsha` is what CMake recorded at
    # CONFIGURE time, so a plain `cmake --build` never refreshes it. Without comparing it against the
    # framework's CURRENT head, a tree rebuilt against newer framework code still reports the OLD commit,
    # matches its pin, and passes -- so a fresh clone would build a different framework than the one just
    # tested, which is the single failure the pin exists to prevent.
    #
    # MEASURED 2026-09-27: this guard is present in 3 of the 10 copies of this file and was absent from the
    # other 7, and the difference is observable. With `psxport_resolved.txt` naming a repo's own recorded
    # pin while the shared framework sits eight commits later, a guarded copy refuses --
    #   "check FAILED -- framework .../psxport is dirty or changed since configure (configured 436c3762,
    #    current ba48b103)" -- and an unguarded one on the same input answers "check OK -- built against
    # e0485d33, which is the recorded pin." Same input, opposite answers, and the wrong one is a pass.
    current = head_of(bdir)
    if current != bsha or dirty(bdir):
        print(f"[psxport] check FAILED — framework {bdir} is dirty or changed since configure "
              f"(configured {bsha}, current {current}). Rebuild from a reconfigure before trusting this "
              f"tree's pin, or bump the pin to what you actually built and tested.")
        return 1
    if bsha == pin:
        print(f"[psxport] check OK — {build} was built against {bsha[:8]}, which is the recorded pin.")
        return 0
    print(f"[psxport] check FAILED — you built against {bsha[:8]} (from {bdir}) but this repo records "
          f"{pin[:8]}.")
    print(f"[psxport]   A fresh clone would build a DIFFERENT framework than you just tested. That is "
          f"how this tree once recorded a pin whose GameHooks lacked a field the game used.")
    print("[psxport]   Fix: reconfigure and verify this build against the recorded pin, "
          "or bump the pin only after verifying a different published framework commit.")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--link", action="store_true", help="point external/psxport at the shared clone")
    g.add_argument("--clone", action="store_true", help="make external/psxport a private clone at the pin")
    g.add_argument("--auto", action="store_true", help="link if a shared clone exists, else clone (run.sh)")
    g.add_argument("--bump", action="store_true", help="record the framework you are building against")
    g.add_argument("--check", action="store_true", help="fail if the built framework is not the pin")
    ap.add_argument("--force", action="store_true", help="allow --link to replace a real clone")
    ap.add_argument("--build", default=CANONICAL_VERIFY_BUILD,
                    help="CMake build directory whose framework receipt to inspect (default: build/ci)")
    args = ap.parse_args()
    if args.link:  return do_link(args)
    if args.clone: return do_clone(args)
    if args.auto:  return do_auto(args)
    if args.bump:  return do_bump(args)
    if args.check: return do_check(args)
    return report(args)


if __name__ == "__main__":
    sys.exit(main())
