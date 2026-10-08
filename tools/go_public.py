#!/usr/bin/env python3
"""go_public.py — pre-publication cleanliness scanner for a git repo.

Scans the FULL git history (not just the working tree) for three classes of
content that must not ship in a public repo, then reports each hit with enough
provenance (blob, path, first/last commit) to decide on remediation. It NEVER
rewrites history or deletes anything — history rewriting + force-push is
destructive and is the user's call (see the skill's SKILL.md for the guarded
remediation flow).

Checks:
  A. copyright  — disc images / ROMs / oversized binaries ever committed
  B. paths      — absolute/home paths outside the repo, baked into blob TEXT
                  (/home, /Users, /mnt, /root, ~/, C:\\, a configured username)
  C. gitignore  — tracked text that references a *sensitive* gitignored item
                  (regenerable ignores like build/ or generated/ are NOT flagged)

Usage:
  go_public.py scan                # all checks over full history -> report
  go_public.py copyright           # only check A
  go_public.py paths               # only check B
  go_public.py gitignore           # only check C
  go_public.py scan --current      # working tree / HEAD only (fast, pre-commit)
  go_public.py scan --json         # machine-readable

Options:
  --current      scan only the working tree + HEAD, not full history
  --json         emit JSON instead of a human report
  --max-bytes N  flag any committed blob larger than N bytes as a binary to
                 review (default 2_000_000; 0 disables the size check)
  -C <dir>       run as if started in <dir> (like git -C)

Exit status: 0 = clean, 1 = findings, 2 = usage/environment error.

Zero dependencies (Python 3 stdlib only). Tune the CONFIG block below per repo.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

# ─────────────────────────── CONFIG (tune per repo) ────────────────────────────

# A. Copyright: path globs that are copyrighted game assets / disc images.
#    Matched against every path that ever existed in history.
COPYRIGHT_PATTERNS = [
    r"\.rvz$", r"\.iso$", r"\.gcm$", r"\.gcz$", r"\.wbfs$", r"\.ciso$",
    r"\.wia$", r"\.nkit(\.iso)?$", r"\.wad$", r"\.nds$", r"\.cia$",
    r"(^|/)rom\.rvz$", r"(^|/)rom\.iso$",
]

# B. Foreign paths: (regex, label, severity) matched against the TEXT of every
#    blob in history. The rule is HARD: NO path outside the repo dir may ship —
#    absolute home paths, ~/ tilde paths, /mnt|/media mounts, Windows paths, and
#    the account usernames are ALL "critical" (blocking). ~/.local/share/... is
#    NOT an acceptable "portable" form — it references the reader's home and must
#    become repo-relative / env-var / a documented default in code, not a baked
#    literal. The "review" severity is retained only as an opt-in escape hatch: a
#    specific per-repo pattern a maintainer deliberately downgrades. Nothing uses
#    it by default. Account names are resolved at runtime (see _names_to_hunt);
#    add extra ones via GO_PUBLIC_NAMES=a,b without editing tracked source.
# The names to hunt for are RESOLVED AT RUNTIME, never baked in as literals. A committed literal
# account name is itself exactly the leak this scanner exists to catch — the audit tool would be the
# thing failing the audit, and worse, publishing it hard-codes one maintainer's identity into every
# clone. Resolve from the environment instead, and let a repo add extra names via GO_PUBLIC_NAMES
# (comma-separated) without editing tracked source.
def _names_to_hunt():
    names = []
    for v in (os.environ.get("USER"), os.environ.get("LOGNAME"),
              os.path.basename(os.environ.get("HOME", "") or "")):
        if v and v not in names and len(v) > 2:
            names.append(v)
    for extra in (os.environ.get("GO_PUBLIC_NAMES", "") or "").split(","):
        extra = extra.strip()
        if extra and extra not in names:
            names.append(extra)
    return names


HUNT_NAMES = _names_to_hunt()
# absolute-path patterns use (?<![\w/]) so a real path boundary is required —
# avoids matching "current/root/system" or "foo/home/bar" mid-word.
FOREIGN_PATH_PATTERNS = [
    (r"(?<![\w/])/home/[A-Za-z0-9_.-]+", "unix home path", "critical"),
    (r"(?<![\w/])/Users/[A-Za-z0-9_.-]+", "macOS home path", "critical"),
    (r"(?<![\w/])/root/[A-Za-z0-9_.-]", "root home path", "critical"),
    *[(r"\b" + re.escape(n) + r"\b", f"account name '{n}'", "critical") for n in HUNT_NAMES],
    (r"(?<![\w/])/mnt/[A-Za-z0-9_.-]+", "mount path", "critical"),
    (r"(?<![\w/])/media/[A-Za-z0-9_.-]+", "media mount path", "critical"),
    # tilde home paths — blocking. ~/.local, ~/.config, ~/Library etc. all count.
    (r"(^|[\s\"'=(])~/[A-Za-z0-9_.-]", "tilde home path", "critical"),
    # windows drive path: drive letter not preceded by an alnum/%/$ (avoids
    # "%d:\n", "first:\n" etc.), one or two backslashes, and the char after must
    # NOT be a C/shell escape letter (n t r b f v a 0 x) — kills format-string
    # false positives like "%d:\n" while still matching real "C:\Users\...".
    (r"(?<![A-Za-z0-9%$])[A-Za-z]:\\{1,2}(?![ntrbfva0xNTRBFVA0X])[A-Za-z_.]",
     "windows path", "critical"),
]
# Paths/globs whose CONTENT is exempt from the foreign-path scan (self-referential
# noise: this scanner names the very patterns it hunts; lockfiles pin abs paths
# that are regenerated). Matched against the blob's path.
FOREIGN_PATH_EXEMPT = [
    r"(^|/)go_public\.py$",
    r"(^|/)\.git/",
    r"(^|/)(package-lock\.json|yarn\.lock|Cargo\.lock|poetry\.lock|pnpm-lock\.yaml)$",
]

# C. Gitignore reference classification. A tracked doc that references a
#    gitignored item is flagged ONLY if that item is PRIVATE DATA — content a
#    public reader can neither regenerate nor supply themselves. Two benign
#    classes are NOT flagged (a doc referencing them is expected and correct):
#      • regenerable output — build/, generated/, scratch/ dumps, *.o, logs …
#      • supply-your-own input — .env (your config), disc images / ROM (yours)
#    Any ignored pattern matching NEITHER benign class is treated as
#    private-data-by-default (fail closed) and its references are flagged.
#    For a well-formed repo whose .gitignore is all output+input, C reads clean;
#    add e.g. `private_notes/` to the ignore and reference it from a committed
#    doc and C will surface it. Retune the two lists per repo.
BENIGN_IGNORE = [
    # regenerable build/output artifacts
    r"^build", r"^generated", r"^port/build", r"^scratch", r"\.o$", r"\.so$",
    r"\.a$", r"\.d$", r"compile_commands\.json", r"\.cache", r"__pycache__",
    r"\.pyc$", r"\.log$", r"\.lock$", r"\.ppm$", r"^fm", r"^sb", r"^ms",
    r"mario_candidates\.txt", r"build\.stale",
    # supply-your-own inputs (the reader provides their own copy)
    r"^\.env", r"\.rvz$", r"\.iso$", r"\.gcm$", r"\.gcz$", r"^rom\.rvz",
]
# Extensions considered "text" for the content scans (B). Everything else is
# treated as binary and skipped for the text scan (but still size-checked in A).
TEXT_EXTS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".s", ".asm",
    ".py", ".sh", ".bash", ".zsh", ".txt", ".md", ".rst", ".json", ".yaml",
    ".yml", ".toml", ".ini", ".cfg", ".cmake", ".mk", ".make", ".ld", ".map",
    ".js", ".ts", ".html", ".css", ".xml", ".env", ".gitignore", ".gitmodules",
    ".gitattributes", ".dox", ".conf", ".rc", ".properties", "",  # extensionless
    "Makefile", "CMakeLists.txt", "Dockerfile",
}

# ─────────────────────────────── plumbing ──────────────────────────────────────


class Fail(Exception):
    pass


def run(args, *, cwd=None, binary=False):
    r = subprocess.run(
        args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False,
    )
    if r.returncode != 0 and not binary:
        raise Fail(f"`{' '.join(args)}` failed: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout if binary else r.stdout.decode("utf-8", errors="replace")


def is_git_repo(cwd):
    r = subprocess.run(["git", "rev-parse", "--is-inside-work-tree"],
                       cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode == 0


def is_text_path(path):
    base = os.path.basename(path)
    if base in TEXT_EXTS:
        return True
    _, ext = os.path.splitext(path)
    return ext.lower() in TEXT_EXTS


def compile_exempt(patterns):
    return [re.compile(p) for p in patterns]


# ───────────────────────── history object walking ──────────────────────────────


def all_history_objects(cwd):
    """Yield (blob_sha, path) for every blob that ever existed in history.

    Deduped on (sha, path). Uses `git rev-list --all --objects` which lists the
    path a blob was last seen at; that's enough for reporting.
    """
    out = run(["git", "rev-list", "--all", "--objects"], cwd=cwd)
    seen = set()
    for line in out.splitlines():
        parts = line.split(" ", 1)
        if len(parts) != 2:
            continue  # commit/tree lines have no path
        sha, path = parts[0], parts[1]
        key = (sha, path)
        if key in seen:
            continue
        seen.add(key)
        yield sha, path


def current_objects(cwd):
    """Yield (blob_sha, path) for tracked files at HEAD/working tree."""
    out = run(["git", "ls-files", "-s"], cwd=cwd)
    for line in out.splitlines():
        # format: <mode> <sha> <stage>\t<path>
        meta, _, path = line.partition("\t")
        bits = meta.split()
        if len(bits) >= 2:
            yield bits[1], path


def blob_size(sha, cwd):
    out = run(["git", "cat-file", "-s", sha], cwd=cwd).strip()
    try:
        return int(out)
    except ValueError:
        return -1


def blob_bytes(sha, cwd):
    return run(["git", "cat-file", "-p", sha], cwd=cwd, binary=True)


def first_last_commits(path, cwd):
    """First and most-recent commit touching a path (across all refs)."""
    out = run(["git", "log", "--all", "--format=%h %ad", "--date=short",
               "--", path], cwd=cwd)
    lines = [l for l in out.splitlines() if l.strip()]
    if not lines:
        return None, None
    return lines[-1], lines[0]  # oldest, newest


# ──────────────────────────────── checks ───────────────────────────────────────


def check_copyright(objects, cwd, max_bytes):
    pats = compile_exempt(COPYRIGHT_PATTERNS)
    findings = []
    for sha, path in objects:
        hit = next((p.pattern for p in pats if p.search(path)), None)
        reason = None
        if hit:
            reason = f"copyrighted-asset pattern /{hit}/"
        elif max_bytes and not is_text_path(path):
            size = blob_size(sha, cwd)
            if size > max_bytes:
                reason = f"large binary blob ({size:,} bytes > {max_bytes:,})"
        if reason:
            first, last = first_last_commits(path, cwd)
            findings.append({
                "check": "copyright", "path": path, "blob": sha[:12],
                "reason": reason, "first_commit": first, "last_commit": last,
            })
    return findings


def check_paths(objects, cwd):
    pats = [(re.compile(rx), label, sev) for rx, label, sev in FOREIGN_PATH_PATTERNS]
    exempt = compile_exempt(FOREIGN_PATH_EXEMPT)
    findings = []
    scanned_blobs = set()
    for sha, path in objects:
        if any(e.search(path) for e in exempt):
            continue
        if not is_text_path(path):
            continue
        if sha in scanned_blobs:
            # same content already scanned under another path; still report path
            pass
        try:
            data = blob_bytes(sha, cwd)
        except Fail:
            continue
        if b"\x00" in data[:8192]:
            continue  # binary despite extension
        text = data.decode("utf-8", errors="replace")
        scanned_blobs.add(sha)
        for i, line in enumerate(text.splitlines(), 1):
            for rx, label, sev in pats:
                m = rx.search(line)
                if m:
                    snippet = line.strip()
                    if len(snippet) > 160:
                        snippet = snippet[:157] + "..."
                    findings.append({
                        "check": "paths", "path": path, "blob": sha[:12],
                        "line": i, "kind": label, "severity": sev,
                        "match": m.group(0), "snippet": snippet,
                    })
    return findings


def parse_gitignore(cwd):
    """Return (sensitive_names, benign_names) from the tracked .gitignore."""
    gi = os.path.join(cwd, ".gitignore")
    benign_rx = compile_exempt(BENIGN_IGNORE)
    sensitive, benign = [], []
    if not os.path.exists(gi):
        return sensitive, benign
    with open(gi, encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#") or line.startswith("!"):
                continue
            (benign if any(b.search(line) for b in benign_rx) else sensitive).append(line)
    return sensitive, benign


def ignore_to_tokens(pattern):
    """Reduce a gitignore pattern to searchable literal tokens.

    e.g. '*.rvz' -> ['.rvz'], 'scratch/' -> ['scratch'], 'rom.rvz' -> ['rom.rvz'].
    Tokens too short/common are dropped to avoid false positives.
    """
    p = pattern.rstrip("/")
    p = p.lstrip("/")
    tokens = []
    if p.startswith("*."):
        tokens.append(p[1:])            # '*.rvz' -> '.rvz'
    elif "*" not in p and "?" not in p:
        tokens.append(p)                # literal name/dir
    else:
        # generic glob: take the longest literal run
        runs = re.split(r"[*?\[\]]", p)
        runs = [r for r in runs if len(r) >= 3]
        tokens.extend(runs)
    return [t for t in tokens if len(t) >= 3]


def check_gitignore(objects, cwd):
    sensitive, _benign = parse_gitignore(cwd)
    token_map = {}  # token -> originating ignore pattern
    for pat in sensitive:
        for tok in ignore_to_tokens(pat):
            token_map.setdefault(tok, pat)
    if not token_map:
        return []
    # build one regex per token as a word-ish literal match
    compiled = [(re.compile(re.escape(tok)), tok, token_map[tok])
                for tok in token_map]
    exempt = compile_exempt(FOREIGN_PATH_EXEMPT + [r"(^|/)\.gitignore$"])
    findings = []
    for sha, path in objects:
        if any(e.search(path) for e in exempt):
            continue
        if not is_text_path(path):
            continue
        try:
            data = blob_bytes(sha, cwd)
        except Fail:
            continue
        if b"\x00" in data[:8192]:
            continue
        text = data.decode("utf-8", errors="replace")
        for i, line in enumerate(text.splitlines(), 1):
            for rx, tok, pat in compiled:
                m = rx.search(line)
                if m:
                    snippet = line.strip()
                    if len(snippet) > 160:
                        snippet = snippet[:157] + "..."
                    findings.append({
                        "check": "gitignore", "path": path, "blob": sha[:12],
                        "line": i, "token": tok, "ignore_pattern": pat,
                        "snippet": snippet,
                    })
    return findings


# ──────────────────────────────── reporting ────────────────────────────────────

BOLD = "\033[1m"; RED = "\033[31m"; YEL = "\033[33m"; GRN = "\033[32m"; DIM = "\033[2m"; OFF = "\033[0m"


def color(s, c):
    return s if not sys.stdout.isatty() else f"{c}{s}{OFF}"


def report(findings, scope):
    by = {"copyright": [], "paths": [], "gitignore": []}
    for f in findings:
        by[f["check"]].append(f)

    print(color(f"go-public scan — {scope}", BOLD))
    print()

    # A
    print(color("A. Copyrighted / binary assets in history", BOLD))
    if not by["copyright"]:
        print(color("   ✓ none found", GRN))
    else:
        for f in by["copyright"]:
            print(color(f"   ✗ {f['path']}", RED))
            span = ""
            if f.get("first_commit"):
                span = f"   [{f['first_commit']}  →  {f['last_commit']}]"
            print(f"       {f['reason']}  blob {f['blob']}{color(span, DIM)}")
    print()

    # B — criticals first, then review-only
    print(color("B. Foreign / absolute paths in blob text", BOLD))
    if not by["paths"]:
        print(color("   ✓ none found", GRN))
    else:
        crit = [f for f in by["paths"] if f.get("severity") == "critical"]
        rev = [f for f in by["paths"] if f.get("severity") != "critical"]
        for tier, items, head, cc in (
            ("critical", crit, "   BLOCKING — outside-repo path, must remove:", RED),
            ("review", rev, "   REVIEW — per-repo opt-in downgrade; confirm intent:", YEL),
        ):
            if not items:
                continue
            print(color(head, cc + BOLD))
            cur = None
            for f in items:
                if f["path"] != cur:
                    cur = f["path"]
                    print(color(f"     {f['path']}  {color('blob ' + f['blob'], DIM)}", cc))
                print(f"       :{f['line']}  {color(f['kind'], cc)}  →  {f['match']}")
                print(color(f"          {f['snippet']}", DIM))
    print()

    # C
    print(color("C. Docs referencing SENSITIVE gitignored items", BOLD))
    if not by["gitignore"]:
        print(color("   ✓ none found", GRN))
    else:
        cur = None
        for f in by["gitignore"]:
            if f["path"] != cur:
                cur = f["path"]
                print(color(f"   ✗ {f['path']}  {color('blob ' + f['blob'], DIM)}", RED))
            print(f"       :{f['line']}  token {color(f['token'], YEL)}  (ignore: {f['ignore_pattern']})")
            print(color(f"          {f['snippet']}", DIM))
    print()

    n = len(findings)
    hard = len(by["copyright"]) + len([f for f in by["paths"]
                                       if f.get("severity") == "critical"])
    soft = n - hard
    if n == 0:
        print(color("RESULT: clean ✓ — ready to publish", GRN + BOLD))
    elif hard == 0:
        print(color(f"RESULT: 0 blocking, {soft} to review — likely OK after a look", YEL + BOLD))
        print(color("        review items are usually portable/intentional; confirm each.", DIM))
    else:
        print(color(f"RESULT: {hard} BLOCKING + {soft} to review — NOT ready to publish",
                    RED + BOLD))
        print(color("        history rewriting is destructive; see SKILL.md remediation flow.", DIM))
    return hard


# ─────────────────────── remediation: replace-text rules ───────────────────────

# Greedy path captures for rule generation (whole path, not just the trigger).
RULE_CAPTURES = [
    r"(?<![\w/])/home/[A-Za-z0-9_./-]+",
    r"(?<![\w/])/Users/[A-Za-z0-9_./-]+",
    r"(?<![\w/])/root/[A-Za-z0-9_./-]+",
    r"(?<![\w/])/mnt/[A-Za-z0-9_./-]+",
    r"(?<![\w/])/media/[A-Za-z0-9_./-]+",
    r"(?<![\w])~/[A-Za-z0-9_./-]+",
]


def gen_rules(cwd):
    """Collect unique machine-specific literals across history and emit a
    starter `git filter-repo --replace-text` rules file. NEVER rewrites; the
    user reviews the right-hand side and runs filter-repo themselves."""
    caps = [re.compile(rx) for rx in RULE_CAPTURES]
    literals = set()
    for sha, path in all_history_objects(cwd):
        if any(e.search(path) for e in compile_exempt(FOREIGN_PATH_EXEMPT)):
            continue
        if not is_text_path(path):
            continue
        try:
            data = blob_bytes(sha, cwd)
        except Fail:
            continue
        if b"\x00" in data[:8192]:
            continue
        text = data.decode("utf-8", errors="replace")
        for rx in caps:
            for m in rx.finditer(text):
                literals.add(m.group(0).rstrip("/.,:;)\"'"))
    # longest first so more-specific paths are replaced before their prefixes
    ordered = sorted(literals, key=len, reverse=True)
    lines = [
        "# git filter-repo --replace-text rules (generated by go_public.py rules)",
        "# REVIEW every right-hand side before running. Format: literal==>replacement",
        "# Longer/more-specific paths are listed first so they win over prefixes.",
        "# Run (in a FRESH clone, after user go-ahead — this REWRITES history):",
        "#   git filter-repo --replace-text <this-file>",
        "",
    ]
    for lit in ordered:
        # SAFETY GATE — never emit a rule too unspecific to be applied blindly.
        #
        # The capture above rstrips trailing punctuation, which can shrink a literal down to
        # something catastrophic as a global text replacement. Real case: the string ~/... inside a
        # CODE COMMENT captured as "~/..." and rstripped of "/." down to a bare "~", which was then
        # emitted as `~==><HOME>`. Fed to filter-repo that rewrites EVERY tilde in EVERY blob of the
        # whole history — every C++ destructor (~Class()), every bitwise NOT (~mask) — silently
        # corrupting the entire codebase to "fix" a comment. It was caught by eye, one command short
        # of being run against a 221-commit public repo.
        #
        # A replace-text rule is only safe if it is specific enough that an accidental match is
        # implausible: it must look like a PATH (contain a separator) and carry real length. Anything
        # else is dropped here rather than left for a reviewer to notice, because this file's whole
        # purpose is to be piped straight into filter-repo.
        if "/" not in lit or len(lit) < 6:
            continue
        # heuristic default replacement — ALWAYS review before running
        if "/repo/" in lit or lit.endswith("/sunbright"):
            repl = "."
        elif "/.claude/" in lit:
            repl = "<local-notes>"
        elif lit.startswith("~/") or lit.startswith("/mnt/") or lit.startswith("/media/"):
            # tilde/mount paths usually need a CODE change (env var / repo-relative),
            # not a literal swap — force the maintainer to decide.
            repl = "<REVIEW-needs-code-change>"
        else:
            repl = "<HOME>"
        lines.append(f"{lit}==>{repl}")
    lines.append("")
    # Bare-username rules are COMMENTED OUT, not live. A bare token replaced across every blob in
    # history also rewrites it inside unrelated words, paths and prose, and the surrounding text has
    # always said "consider" — emitting them active contradicted that and made an opt-in read as a
    # default. Uncomment deliberately, after checking what the token actually matches.
    lines.append("# also consider bare usernames (uncomment ONLY after checking what they match:")
    lines.append("#   git grep -I -n <name> $(git rev-list --all) -- | head)")
    lines.append(f"# {USERNAME}==>user")
    lines.append(f"# {USERNAME_ALT}==>user")
    return "\n".join(lines)


# ──────────────────────────────────── main ─────────────────────────────────────


def main(argv=None):
    ap = argparse.ArgumentParser(description="pre-publication git-history cleanliness scanner")
    ap.add_argument("cmd", nargs="?", default="scan",
                    choices=["scan", "copyright", "paths", "gitignore", "rules"])
    ap.add_argument("-o", dest="outfile", default=None,
                    help="rules: write to this file instead of stdout")
    ap.add_argument("--current", action="store_true",
                    help="scan working tree/HEAD only, not full history")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--max-bytes", type=int, default=2_000_000)
    ap.add_argument("-C", dest="chdir", default=".")
    args = ap.parse_args(argv)

    cwd = os.path.abspath(args.chdir)
    if not is_git_repo(cwd):
        print(f"error: not a git repo: {cwd}", file=sys.stderr)
        return 2

    if args.cmd == "rules":
        try:
            text = gen_rules(cwd)
        except Fail as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        if args.outfile:
            with open(args.outfile, "w", encoding="utf-8") as fh:
                fh.write(text + "\n")
            print(f"wrote replace-text rules to {args.outfile} — REVIEW before running filter-repo")
        else:
            print(text)
        return 0

    try:
        objects = list(current_objects(cwd) if args.current else all_history_objects(cwd))
    except Fail as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    findings = []
    if args.cmd in ("scan", "copyright"):
        findings += check_copyright(objects, cwd, args.max_bytes)
    if args.cmd in ("scan", "paths"):
        findings += check_paths(objects, cwd)
    if args.cmd in ("scan", "gitignore"):
        findings += check_gitignore(objects, cwd)

    scope = "working tree + HEAD" if args.current else "FULL history (all refs)"
    if args.json:
        print(json.dumps({"scope": scope, "findings": findings,
                          "clean": len(findings) == 0}, indent=2))
        return 1 if findings else 0

    n = report(findings, scope)
    return 1 if n else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fail as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        sys.exit(130)
