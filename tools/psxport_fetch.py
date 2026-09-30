#!/usr/bin/env python3
"""psxport_fetch.py — the ONE file a port ships: point `external/psxport` at the PINNED framework. CANONICAL.

A port must build from a bare clone of itself, so it cannot reach into the framework before it has the
framework. This is that bootstrap step and nothing else. The pin CHECK, the build receipt, the report
and the bump live in psxport's `tools/psxport_sync.py`, which a port runs out of the fetched checkout as
`external/psxport/tools/psxport_sync.py --repo .` — disjoint jobs, one shipped file, and no import
between them, because this one must work with no framework on disk yet.

WHY A PIN WORKTREE AND NOT A SYMLINK TO THE MOVING CHECKOUT
----------------------------------------------------------
MEASURED 2026-09-30 16:34 (issue 0142 carries the bytes): `--auto` run from a title's LINKED git worktree
destroyed the SHARED checkout — its `vendor/beetle-psx` working tree AND its `.git/modules/vendor/beetle-psx`
were both deleted. Two causes, both visible below: discovery that could not name the shared checkout from
a worktree, and a `git submodule update` whose cwd resolved through a symlink to that checkout.

The deeper half of that lesson is the one a link hides: a symlink at `external/psxport` points at a MOVING
checkout, so every title's build silently follows whatever that checkout is at, and a framework commit
landing under a running consumer is indistinguishable from the consumer having been broken. So:

  **external/psxport resolves to a checkout of the PINNED commit, never to the moving one.**

On a machine with the shared checkout, that pinned tree is a detached git worktree of the shared
repository at `<shared>/scratch/pins/<full-sha>/`, created once and reused while it is clean at that
commit. It is reused, never repaired: one that is dirty, or at another commit, is a REFUSAL, because
silently re-pointing a title at a different framework than the one it was verified against is the exact
failure the pin exists to prevent. With no shared checkout at all, this falls back to a private clone at
the pin, staged in a sibling and published by one atomic rename.

And the second rule, unchanged and still absolute: **no git command runs with its cwd at
`external/psxport`, or through any symlink this tool did not create.**
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

LINK_REL = os.path.join("external", "psxport")
PIN_NAME = "psxport.pin"
PIN_ROOT_REL = os.path.join("scratch", "pins")
DEFAULT_URL = "https://github.com/SomeoneIsWorking/psxport.git"
# psxport's own vendor submodules, init'd non-recursively (beetle has a URL-less nested gitlink that
# makes `--recursive` fail outright), then that nested gitlink, as (parent, child) pairs.
SUBMODULES = ((".", "vendor/beetle-psx"), (".", "vendor/lucent"), ("vendor/beetle-psx", "deps/libchdr"))
LIGHTREC_REVISION = re.compile(r'set\s*\(\s*PSXPORT_LIGHTREC_REVISION\s+"([0-9a-f]{40})"')


def init_submodules(root):
    """Every declared submodule, non-recursively, one level at a time and always with cwd INSIDE
    `root`. Beetle has a URL-less nested gitlink that makes `--recursive` fail outright."""
    for parent, sub in SUBMODULES:
        path = os.path.join(root, parent)
        if not os.path.isdir(path):
            print(f"[psxport] NOTE: that revision has no {parent}, so {sub} was NOT initialised.")
            continue
        git(["submodule", "update", "--init", sub], path)


def git(args, cwd):
    done = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return done.stdout.strip(), done.returncode


def git_failure(args, cwd):
    """git's own error text for a command that just failed, so a refusal can quote it instead of
    hiding it behind a paraphrase."""
    done = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return (done.stderr or done.stdout).strip() or f"exit status {done.returncode}, no output"


def is_framework(path):
    return os.path.isfile(os.path.join(path, "cmake", "psxport.cmake"))


def describe_link(link):
    """symlink | clone | dir | missing — what the path IS; never guessed, 'missing' is a real answer."""
    if os.path.islink(link):
        return "symlink"
    if os.path.isdir(link):
        inner = os.path.join(link, ".git")
        return "clone" if os.path.isdir(inner) or os.path.isfile(inner) else "dir"
    return "missing"


def shared_candidates(repo):
    """$PSX first, then the MAIN checkout's sibling, then this repository's own sibling.

    `git rev-parse --git-common-dir` tells a linked worktree from a normal checkout: `.git` inside the
    tree, versus the MAIN checkout's `.git`. Its parent is the main checkout root, and that directory's
    sibling is the shared framework — the candidate the 2026-09-30 incident lacked, because in a
    worktree `REPO/..` is `<title>/scratch/wt`, which holds no framework. Deduplicated on the resolved
    path, first spelling and position kept; a candidate that IS `repo` is dropped.
    """
    out = []
    if os.environ.get("PSX"):
        out.append(os.path.join(os.environ["PSX"], "psxport"))
    common, rc = git(["rev-parse", "--git-common-dir"], repo)
    if rc == 0 and common:
        common = common if os.path.isabs(common) else os.path.join(repo, common)
        out.append(os.path.join(os.path.dirname(os.path.abspath(common)), "..", "psxport"))
    out.append(os.path.join(os.path.abspath(repo), "..", "psxport"))
    seen, unique = set(), []
    for cand in out:
        key = os.path.realpath(cand)
        if key not in seen and key != os.path.realpath(repo):
            seen.add(key)
            unique.append(cand)
    return unique


def read_pin(pin_file):
    """(url, commit) or (None, None). A missing or malformed pin is a refusal, never a default."""
    if not os.path.isfile(pin_file):
        return None, None
    url, commit = DEFAULT_URL, None
    with open(pin_file, encoding="utf-8") as handle:
        for line in handle:
            found = re.match(r"(\w+)\s*=\s*(\S+)", line.split("#", 1)[0].strip())
            if found and found.group(1) in ("url", "commit"):
                url, commit = (found.group(2), commit) if found.group(1) == "url" else (url, found.group(2))
    return url, commit


def pin_worktree_path(repo, sha):
    """Where the pinned checkout of `repo` at `sha` lives: under its own gitignored `scratch/`, so
    creating one cannot make the shared checkout look dirty to its own status checks."""
    return os.path.join(repo, PIN_ROOT_REL, sha)


def pin_worktree_usable(repo, path, sha):
    """(usable, why). The three questions before REUSING an existing pinned worktree: is it at the
    pinned commit, does it belong to that repository, and is it clean. Any other answer is a refusal and
    the path is left exactly as it is — repairing it would silently re-point a title at a different
    framework than the one its pin names."""
    head, rc = git(["rev-parse", "HEAD"], path)
    if rc != 0 or not head:
        return False, f"it is not a git checkout ({describe_link(path)})"
    if head != sha:
        return False, f"it is at {head[:12]}, not the pinned {sha[:12]}"
    common, rc = git(["rev-parse", "--git-common-dir"], path)
    if rc != 0 or not os.path.samefile(os.path.realpath(os.path.join(path, common)),
                                       os.path.realpath(os.path.join(repo, ".git"))):
        return False, f"it belongs to another repository ({common or 'unknown'})"
    out, rc = git(["status", "--porcelain", "--untracked-files=all"], path)
    if rc != 0:
        return False, f"its state cannot be read: {git_failure(['status', '--porcelain'], path)}"
    if out:
        return False, f"it has {len(out.splitlines())} uncommitted change(s): {out.splitlines()[0]}"
    return True, "clean at the pinned commit"


def submodule_gitdirs(root):
    """Every initialised submodule working tree under `root` (nested ones too) as (worktree, gitdir),
    read from each one's `.git` gitfile and never by running git inside it: that is exactly the command
    that fails while `core.worktree` is stale."""
    for parent, sub in SUBMODULES:
        work = os.path.join(root, parent, sub)
        gitfile = os.path.join(work, ".git")
        if not os.path.isfile(gitfile):
            continue
        with open(gitfile, encoding="utf-8") as handle:
            text = handle.read().strip()
        if text.startswith("gitdir:"):
            yield work, os.path.normpath(os.path.join(work, text[len("gitdir:"):].strip()))


def repair_published_links(repo, target):
    """Re-point the git metadata that still names the STAGING path at the published one.

    The rename moves the directory and nothing else. Git recorded the staging path in the worktree's
    admin `gitdir` link (`git worktree repair` rewrites that) and in each submodule's `core.worktree`
    (relative to its module gitdir, so it must be recomputed). The submodules' own gitfiles are relative
    to the moved tree and need no change. Returns an error text, or None."""
    _, rc = git(["worktree", "repair", target], repo)
    if rc != 0:
        return f"git worktree repair failed: {git_failure(['worktree', 'repair', target], repo)}"
    for work, gitdir in submodule_gitdirs(target):
        value = os.path.relpath(work, gitdir)
        _, rc = git(["config", "--file", os.path.join(gitdir, "config"), "core.worktree", value], repo)
        if rc != 0:
            return f"could not re-point core.worktree of {work}"
    return None


def ensure_pin_worktree(repo, sha, with_submodules=True):
    """The pinned detached worktree of `repo` at `sha`, creating it if absent. (path, note) or
    (None, why).

    Creation is `git worktree add --detach` into a TEMP sibling, submodule init inside that temporary
    worktree only, then ONE atomic rename into the pinned path. The shared checkout's own working tree
    and its `.git/modules` are never a cwd for a git command, which is the incident.
    """
    target = pin_worktree_path(repo, sha)
    if os.path.lexists(target):
        usable, why = pin_worktree_usable(repo, target, sha)
        return (target, "reused") if usable else (None, why)
    pins_root = os.path.dirname(target)
    os.makedirs(pins_root, exist_ok=True)
    staging = tempfile.mkdtemp(prefix=".pin-", dir=pins_root)
    os.rmdir(staging)                     # `git worktree add` wants the path free, not empty
    try:
        _, rc = git(["worktree", "add", "--detach", staging, sha], repo)
        if rc != 0:
            return None, f"git worktree add --detach {sha[:12]} failed in {repo}"
        init_submodules(staging)
        if os.path.lexists(target):
            return None, "another actor created it while the pinned worktree was being built"
        try:
            os.rename(staging, target)
        except OSError as error:
            # Two titles pinned to the same commit, fetching at the same time: both saw the path free,
            # and the loser's rename fails rather than merging two trees. That is a refusal, not a crash.
            return None, f"another actor published it first ({error.strerror or error})"
        broken = repair_published_links(repo, target)
        if broken is None:
            usable, why = pin_worktree_usable(repo, target, sha)
            broken = None if usable else why
        if broken is not None:
            # This tool's own pristine creation, unusable: remove it so the next run builds afresh
            # rather than refusing forever on a tree nobody has touched.
            git(["worktree", "remove", "--force", target], repo)
            shutil.rmtree(target, ignore_errors=True)
            return None, f"the new pinned worktree was unusable after publication ({broken})"
        return target, "created"
    finally:
        if os.path.isdir(staging):        # only ever this tool's own staging path; a rename consumed it
            git(["worktree", "remove", "--force", staging], repo)
            shutil.rmtree(staging, ignore_errors=True)


def point_link(link, target):
    """Point `link` at `target` with ONE atomic rename. Never replaces a non-symlink: a real clone or
    directory there is somebody's work, and this tool has never been asked to delete one."""
    if os.path.lexists(link) and not os.path.islink(link):
        print(f"[psxport] REFUSED: external/psxport is a {describe_link(link)}; a pinned link never "
              f"replaces one. Inspect it, then remove it yourself and re-run.")
        return 2
    if os.path.islink(link) and os.path.realpath(link) == os.path.realpath(target):
        return 0                         # already pointing at the pinned checkout
    os.makedirs(os.path.dirname(link), exist_ok=True)
    staging = os.path.join(os.path.dirname(link), f".psxport-link-{os.getpid()}")
    if os.path.lexists(staging):
        os.unlink(staging)
    os.symlink(os.path.relpath(target, os.path.dirname(link)), staging)
    os.rename(staging, link)             # replaces a symlink atomically; never a directory
    return 0


def do_ensure(repo, link, pin_file):
    """Point external/psxport at a checkout of the PINNED commit: a pinned worktree of the shared
    checkout when this machine has one, else a private clone at the same pin."""
    _, pin = read_pin(pin_file)
    if not pin:
        print("[psxport] REFUSED: no usable psxport.pin, so there is no commit to point at.")
        return 2
    for cand in shared_candidates(repo):
        if not is_framework(cand):
            continue
        target, note = ensure_pin_worktree(cand, pin)
        if target is None:
            print(f"[psxport] REFUSED: the pinned worktree for {pin[:12]} under {cand} is unusable — "
                  f"{note}. It was NOT touched. Inspect it, then remove it yourself and re-run.")
            return 2
        status = point_link(link, target)
        if status == 0 and note == "created":
            print(f"[psxport] pinned worktree {note} at {pin[:12]} -> {target}")
        if status == 0:
            print(f"[psxport] external/psxport -> {target}  ({note}; framework edits are NOT live here)")
        return status
    kind = describe_link(link)
    if kind == "symlink" and is_framework(link):
        # A link to a live framework checkout, left by a person or another tool: with no shared
        # checkout discoverable from here it is the only framework this title has, and replacing it
        # with a clone would discard a deliberate choice.
        print(f"[psxport] external/psxport is a symlink to {os.path.realpath(link)}; left alone.")
        return 0
    if kind == "clone" and is_framework(link):
        return advance_private_clone(link, pin)
    return do_clone(link, pin_file)


def advance_private_clone(link, pin):
    """Bring a private clone at `link` to the pinned commit, or refuse naming both commits.

    MEASURED 2026-10-01: a title's private clone sat at the previous pin after `psxport.pin` moved, the
    tool exited 0 silently, and the build failed on a header only the pinned framework has. A clone left
    at an older commit is a stale build wearing a valid path's name, so this moves it — and it moves it
    only when that discards nothing: a clone with uncommitted or untracked files is REFUSED and left
    byte-for-byte as found. The commit is fetched from `origin` when the clone does not have it; one the
    remote does not have either is a refusal, because nothing can make a fresh clone build it.
    """
    head, rc = git(["rev-parse", "HEAD"], link)
    head = head if rc == 0 and head else None
    if head == pin:
        return 0
    shown = head[:12] if head else "an unreadable commit"
    changes, rc = git(["status", "--porcelain", "--untracked-files=all"], link)
    if rc != 0 or changes:
        count = len(changes.splitlines()) if rc == 0 else "unreadable"
        print(f"[psxport] REFUSED: external/psxport is a private clone at {shown}, not the pinned "
              f"{pin[:12]}, and it has {count} uncommitted change(s), so it was NOT moved. Commit or "
              f"discard them yourself, or delete external/psxport, then re-run.")
        return 2
    if git(["cat-file", "-e", f"{pin}^{{commit}}"], link)[1] != 0:
        print(f"[psxport] private clone at {shown} lacks the pinned {pin[:12]}; fetching origin")
        git(["fetch", "origin"], link)
    if git(["checkout", "--detach", pin], link)[1] != 0:
        print(f"[psxport] REFUSED: external/psxport is a private clone at {shown}, not the pinned "
              f"{pin[:12]}, and that commit is not reachable from its origin, so it was NOT moved. "
              f"Push the framework commit, then re-pin.")
        return 2
    init_submodules(link)
    print(f"[psxport] private clone advanced {shown} -> {pin[:12]}")
    return 0


def do_clone(link, pin_file):
    """No shared checkout on this machine: stage the clone in a SIBLING of `link` — one filesystem, so
    publishing is an atomic rename — and run every git command there."""
    url, pin = read_pin(pin_file)
    if not pin:
        print("[psxport] REFUSED: no usable psxport.pin, so there is no commit to clone to.")
        return 2
    if os.path.islink(link):
        os.unlink(link)                   # a symlink is this tool's own earlier work, or another's
    elif os.path.lexists(link):
        print("[psxport] REFUSED: external/psxport exists and is not a symlink. Inspect it, then "
              "remove it yourself and re-run.")
        return 2
    os.makedirs(os.path.dirname(link), exist_ok=True)
    staging = tempfile.mkdtemp(prefix=".psxport-clone-", dir=os.path.dirname(link))
    published = False
    try:
        print(f"[psxport] staging a private clone of {url} at pin {pin}")
        if git(["clone", url, staging], os.path.dirname(link))[1] != 0:
            print("[psxport] REFUSED: clone failed.")
            return 2
        if git(["checkout", pin], staging)[1] != 0:
            print(f"[psxport] REFUSED: pin {pin} is not reachable in {url}, so a fresh clone of this "
                  f"repo CANNOT build. Push the framework commit, then re-pin.")
            return 1
        init_submodules(staging)
        if not os.path.lexists(link):
            try:
                os.rename(staging, link)
                published = True
            except OSError as error:       # another clone landed in the same window
                print(f"[psxport] REFUSED: external/psxport was taken while the clone was in progress "
                      f"({error.strerror or error}). The staged clone was discarded.")
        else:
            print(f"[psxport] REFUSED: external/psxport appeared while the clone was in progress and is "
                  f"now a {describe_link(link)}. Nothing was moved and the staged clone was discarded.")
        if published:
            print(f"[psxport] external/psxport cloned at pin {pin}")
        return 0 if published else 2
    finally:
        if not published:
            shutil.rmtree(staging, ignore_errors=True)


def pinned_lightrevision(repo):
    """The Lightrec revision psxport pins, read from the module that consumes it — ONE source for the
    pin, so this tool and cmake/lightrec_dependency.cmake cannot disagree about it."""
    source = os.path.join(repo, "cmake", "lightrec_dependency.cmake")
    if not os.path.isfile(source):
        return None
    with open(source, encoding="utf-8") as handle:
        found = LIGHTREC_REVISION.search(handle.read())
    return found.group(1) if found else None


def do_lightrec(repo):
    """Ensure the pinned Lightrec worktree exists, and print its path.

    Lightrec gets the SAME treatment for the same reason: a consumer must not follow a moving checkout.
    `cmake/lightrec_dependency.cmake` resolves `<shared>/scratch/pins/<sha>/` before the plain shared
    checkout and still refuses a checkout whose HEAD is not the pinned revision, so a Lightrec commit
    landing under a running consumer cannot change what that consumer builds.
    """
    sha = pinned_lightrevision(repo)
    if not sha:
        print("[psxport] REFUSED: no PSXPORT_LIGHTREC_REVISION in cmake/lightrec_dependency.cmake, so "
              f"there is no Lightrec commit to pin to.")
        return 2
    roots = []
    if os.environ.get("SHARED_DIR"):
        roots.append(os.path.join(os.environ["SHARED_DIR"], "lightrec"))
    parent = os.path.dirname(os.path.abspath(repo))
    roots += [os.path.join(parent, "shared", "lightrec"), os.path.join(os.path.dirname(parent),
                                                                      "shared", "lightrec")]
    for candidate in roots:
        if not os.path.isfile(os.path.join(candidate, "CMakeLists.txt")):
            continue
        target, note = ensure_pin_worktree(candidate, sha, with_submodules=False)
        if target is None:
            print(f"[psxport] REFUSED: the pinned Lightrec worktree for {sha[:12]} under {candidate} is "
                  f"unusable — {note}. It was NOT touched.")
            return 2
        print(f"[psxport] Lightrec worktree {note} at {sha[:12]} -> {target}")
        return 0
    print("[psxport] REFUSED: no shared Lightrec checkout found. Looked in: " + ", ".join(roots))
    return 2


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    help="title repository whose external/psxport must exist (default: this file's repo)")
    ap.add_argument("--auto", action="store_true",
                    help="the default and only action: pin to the recorded commit, cloning if need be")
    ap.add_argument("--lightrec", action="store_true",
                    help="instead, ensure the pinned Lightrec worktree and print its path")
    args = ap.parse_args(argv)
    repo = os.path.abspath(args.repo)
    if args.lightrec:
        return do_lightrec(repo)
    return do_ensure(repo, os.path.join(repo, LINK_REL), os.path.join(repo, PIN_NAME))


if __name__ == "__main__":
    sys.exit(main())
