#!/usr/bin/env python3
"""psxport_sync.py — resolve `external/psxport`, and keep the recorded pin honest.

WHY THIS REPLACED THE SUBMODULE (2026-08-16). The framework used to be a git submodule. Two incidents
in one day came directly from that mechanism:

  * The tree was BUILT against psxport 25dd7826 while RECORDING a1c53d7c, so a bare clone did not
    compile — the game's hook table named a field the pinned framework did not have. Nothing noticed,
    because a submodule working tree and its recorded gitlink drift silently.
  * "Fixing" that drift by syncing to the recorded pin is what pulled a broken beetle GTE commit into
    the working build, which had already broken PSXPORT_ORACLE=1 in every 3D scene for two days. That
    commit had been made on a DETACHED HEAD inside the submodule — which is the default state of a
    submodule checkout, and is how it was never reviewed.

Also, `git submodule update --recursive` simply FAILS on this tree: beetle-psx has a URL-less nested
gitlink (`deps/lightning/gnulib`) that git itself cannot resolve.

WHAT REPLACED IT. `external/psxport` is no longer tracked. It is either

  * a SYMLINK to the workspace's shared framework clone (the local default — every port then runs off
    one writable checkout, so an edit is live everywhere immediately), or
  * a real CLONE checked out at the pin (fresh machine, CI, or anyone cloning this repo alone).

The PATH does not change, so all 151 files that reference `external/psxport/...` keep working, and
`PSXPORT_DIR` still defaults to it.

WHAT THE PIN IS FOR. `psxport.pin` records the framework commit this game was built and verified
against. Ports are deliberately NOT all on framework HEAD — measured 2026-08-16, six ports spanned 55
commits of framework history — because with one maintainer, pins are what let one port be worked on
daily while the others sit untouched. Dropping them would have broken all six the moment that beetle
GTE commit landed. The pin is provenance and the fresh-clone fallback; it is not what you build against
day to day.

Exit codes: 0 ok · 1 the check failed (drift, or a pin a fresh clone could not use) · 2 refused,
because the tool could not assert anything.
"""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = os.path.join(REPO, "external", "psxport")
PIN = os.path.join(REPO, "psxport.pin")
RESOLVED = os.path.join(REPO, "build", "psxport_resolved.txt")
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
    url, pin = read_pin()
    sha = head_of(target) if kind in ("symlink", "clone") else None
    print(f"[psxport] external/psxport : {kind}" + (f" -> {target}" if kind == "symlink" else ""))
    print(f"[psxport] framework HEAD   : {sha or '(none)'}"
          + ("  +dirty" if sha and dirty(target) else ""))
    print(f"[psxport] recorded pin     : {pin or '(no psxport.pin)'}")
    built = read_resolved()
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


def read_resolved():
    """(dir, sha) the last cmake configure resolved, or None. Written by CMakeLists."""
    if not os.path.isfile(RESOLVED):
        return None
    d = s = None
    for line in open(RESOLVED):
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
    kind, target = describe_link()
    sha = head_of(target)
    if not sha:
        print("[psxport] REFUSED: external/psxport has no resolvable HEAD — nothing to record.")
        return 2
    if dirty(target):
        print("[psxport] REFUSED: the framework checkout is DIRTY. Recording a pin now would name a "
              "commit that does not describe what you built. Commit the framework first.")
        return 1
    url, old = read_pin()
    remote_has, rc = git(["branch", "-r", "--contains", sha], target)
    if rc != 0 or not remote_has.strip():
        print(f"[psxport] REFUSED: {sha[:8]} is not on any remote branch. Recording it would leave a "
              f"pin that a fresh clone cannot fetch — which is exactly how this repo shipped a tree "
              f"that did not build standalone. Push the framework first.")
        return 1
    write_pin(url or DEFAULT_URL, sha)
    print(f"[psxport] pin {(old or '(none)')[:8]} -> {sha[:8]}")
    return 0


def do_check(args):
    """The precommit check: what you BUILT against must be what this repo RECORDS."""
    url, pin = read_pin()
    if not pin:
        print("[psxport] REFUSED: no psxport.pin — this check asserted NOTHING.")
        return 2
    built = read_resolved()
    if not built:
        print(f"[psxport] check: no build/psxport_resolved.txt — this tree has not been configured, so "
              f"there is nothing to compare the pin against. Asserting nothing (pin {pin[:8]}).")
        return 0
    bdir, bsha = built
    if bsha == pin:
        print(f"[psxport] check OK — built against {bsha[:8]}, which is the recorded pin.")
        return 0
    print(f"[psxport] check FAILED — you built against {bsha[:8]} (from {bdir}) but this repo records "
          f"{pin[:8]}.")
    print(f"[psxport]   A fresh clone would build a DIFFERENT framework than you just tested. That is "
          f"how this tree once recorded a pin whose GameHooks lacked a field the game used.")
    print(f"[psxport]   Fix:  python3 tools/psxport_sync.py --bump")
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
    args = ap.parse_args()
    if args.link:  return do_link(args)
    if args.clone: return do_clone(args)
    if args.auto:  return do_auto(args)
    if args.bump:  return do_bump(args)
    if args.check: return do_check(args)
    return report(args)


if __name__ == "__main__":
    sys.exit(main())
