#!/usr/bin/env python3
"""info.py — the project information system's entry point + the two ledgers nothing else keeps.

WHY THIS EXISTS. A project accumulates three kinds of durable knowledge that ALREADY have homes:
  · symptom -> cause/dead-end        docs/issues/       (issue-catalog skill, catalog.py)
  · subsystem -> where/what status   docs/code-map.md   (codemap skill, codemap.py)
  · RE step -> real or hack          docs/re-frontier.md(re-frontier skill, re_frontier.py)
and two kinds that have NO home, which is where sessions actually lose time:

  CLAIMS      "X is verified" + the evidence + whether it still holds. A claim cited as proof and
              later falsified silently poisons everything that leaned on it. Real examples: an
              "item menu 0/76800 vs the reference renderer" number quoted all day as proof the menu
              was correct (it only proved we MATCH the reference, which was itself wrong); an
              "SBS 0-diff" citation that was stale; a "duplicate ownership swept clean" that had
              been measured against a stale binary.
  INSTRUMENTS the tools that produce evidence, and whether they can be trusted. A broken instrument
              fails SILENTLY — "no signal" and "instrument returning nothing" look identical. Six
              were found broken in ONE session: a screenshot tool returning black at 60fps, a
              z-fight ranker using > where the rasterizer uses >=, a sweep whose reference leg was a
              no-op, a build check whose grep could not match the compiler's error format, a verify
              mode that suppressed the very overrides it was gating, and an arg parsed as decimal so
              a run checked nothing and came back green.

Memories do not cover these: they are per-machine, unsearchable at the moment of need, invisible to
subagents, and they record conclusions rather than the evidence a conclusion rests on.

DATA LIVES IN THE REPO (docs/info/), greppable Markdown, one file per entry — so it travels with the
code, reaches subagents, and survives without this tool.

USAGE
  info.py brief <words...>      # START HERE. One query across every registry: issues, claims,
                                # instruments, codemap, re-frontier. Replaces remembering 3 CLIs.
  info.py claim add "<claim>" --evidence "<how proven>" --falsified-by "<what OBSERVATION would disprove it>"
                              [--depends path[#symbol] ...]
  info.py claim list [--stale] [--falsified]
  info.py claim check [--strict] [--verbose] [--selftest]   # has the code a claim rests on MOVED?
  info.py claim falsify <id> --why "<what disproved it>"
  info.py claim confirm <id> --evidence "<re-proof>"    # also RESETS the staleness baseline
  info.py instrument add "<tool>" --validated "<how you proved it can show the OTHER answer>"
  info.py instrument distrust <id> --why "<failure mode>"
  info.py instrument list [--distrusted]
  info.py check                 # non-zero if a distrusted instrument or falsified claim is in play
"""
import argparse
import re, datetime, json, os, re, subprocess, sys, textwrap

ROOT = os.getcwd()
INFO = os.path.join(ROOT, "docs", "info")
CLAIMS, INSTR = os.path.join(INFO, "claims"), os.path.join(INFO, "instruments")
# Resolved from the environment, never a baked home-relative literal: a tilde-prefixed path names the
# READER's home, which the publication audit blocks by rule (and rightly — it is not a portable form).
# That rule applies to this comment too, so the pattern is described here rather than spelled out.
# Override with CLAUDE_SKILLS_DIR.
SKILLS = os.environ.get("CLAUDE_SKILLS_DIR") or os.path.join(os.environ.get("HOME", ""), ".claude", "skills")


def today():
    return datetime.date.today().isoformat()


def slug(s, n=48):
    s = re.sub(r"[^a-zA-Z0-9]+", "-", s.lower()).strip("-")
    return (s[:n] or "entry").rstrip("-")


def ensure(d):
    os.makedirs(d, exist_ok=True)


def next_id(d, prefix):
    """Next free sequential id, SKIPPING any already taken.

    Parallel agents each compute this against their OWN worktree, so a naive max()+1 hands the same
    number to every one of them — that produced three entries all called I003 in a single round (the
    same collision the kanban cards hit). Scanning for the first FREE slot means the operator's
    integration renumbers at most the genuine duplicates instead of silently overwriting one.
    """
    ensure(d)
    taken = {int(m.group(1)) for f in os.listdir(d) if (m := re.match(r"(\d+)-", f))}
    n = 1
    while n in taken:
        n += 1
    return f"{prefix}{n:03d}", n


def write(path, front, body):
    with open(path, "w") as f:
        f.write("---\n")
        for k, v in front.items():
            f.write(f"{k}: {v}\n")
        f.write("---\n\n" + body.rstrip() + "\n")


def parse(path):
    txt = open(path).read()
    front, body = {}, txt
    if txt.startswith("---"):
        _, fm, body = txt.split("---", 2)
        for line in fm.strip().splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                front[k.strip()] = v.strip()
    return front, body.strip()


def entries(d):
    ensure(d)
    out = []
    for f in sorted(os.listdir(d)):
        if f.endswith(".md"):
            p = os.path.join(d, f)
            fr, bo = parse(p)
            fr["_path"], fr["_body"] = p, bo
            out.append(fr)
    return out


def by_id(d, i):
    for e in entries(d):
        if e.get("id", "").lower() == i.lower() or e.get("id", "").lower().endswith(i.lower().lstrip("ci")):
            return e
    sys.exit(f"info: no entry {i} in {d}")


# ---------------------------------------------------------------- claims
def cmd_claim_add(a):
    ensure(CLAIMS)
    cid, n = next_id(CLAIMS, "C")
    p = os.path.join(CLAIMS, f"{n:03d}-{slug(a.claim)}.md")
    front = {"id": cid, "kind": "claim", "status": "holds", "created": today(),
             "tags": ",".join(a.tag or [])}
    if a.depends:
        # The single field that makes rot MECHANICALLY detectable: name the code the evidence
        # rests on and "has it changed since?" becomes a git query (see cmd_claim_check).
        front["depends"] = ", ".join(a.depends)
    write(p, front,
          f"## Claim\n\n{a.claim}\n\n## Evidence\n\n{a.evidence}\n\n## What would falsify it\n\n"
          f"{a.expires_on or 'UNSPECIFIED — a claim with no falsifier is a belief, not a result.'}\n")
    print(f"{cid}  {p}")


def cmd_claim_list(a):
    for e in entries(CLAIMS):
        st = e.get("status", "?")
        if a.stale and st != "stale":
            continue
        if a.falsified and st != "falsified":
            continue
        mark = {"holds": "✓", "falsified": "✗", "stale": "~"}.get(st, "?")
        print(f"{mark} {e.get('id')}  [{st}]  {e['_body'].split(chr(10))[2][:90] if len(e['_body'].split(chr(10)))>2 else ''}")


def cmd_claim_falsify(a):
    e = by_id(CLAIMS, a.id)
    fr, bo = parse(e["_path"])
    fr["status"] = "falsified"
    fr["falsified_on"] = today()
    write(e["_path"], fr, bo + f"\n\n## FALSIFIED {today()}\n\n{a.why}\n\n"
          f"> Anything that cited this claim as proof must be re-checked. Grep the repo for it.\n")
    print(f"{e.get('id')} falsified. Now grep for who relied on it:")
    key = bo.split("## Claim")[-1].split("##")[0].strip().splitlines()[0][:40] if "## Claim" in bo else ""
    if key:
        print(f"  git grep -n {key.split()[0]!r} -- docs/ || true")


def cmd_claim_confirm(a):
    e = by_id(CLAIMS, a.id)
    fr, bo = parse(e["_path"])
    fr["status"] = "holds"
    fr["reconfirmed"] = today()
    # Re-confirming RESETS the staleness baseline. Without this, a claim re-proved today would keep
    # being reported stale by every future `claim check` because its ADD commit is still the anchor —
    # and a checker that cries wolf after the wolf was dealt with stops being read.
    fr["verified_at"] = today()
    if a.depends:
        fr["depends"] = ", ".join(a.depends)
    write(e["_path"], fr, bo + f"\n\n## Re-confirmed {today()}\n\n{a.evidence}\n")
    print(f"{e.get('id')} re-confirmed (staleness baseline reset to {today()})")
    if not fr.get("depends"):
        print("  NOTE: no `depends:` recorded — this claim stays INVISIBLE to `info.py claim check`. "
              "Add one: --depends path/to/file.cpp#symbol")


# ------------------------------------------------------- claim staleness
# WHY. The issue catalog pulls resolutions forward: a resolved issue READS resolved. The claims
# ledger had no equivalent — a claim marked `holds` stayed `holds` forever unless a human remembered
# to falsify it. Real cost, twice in one day: C099 ("the logo screens are black because gpu_vk.cpp's
# render_geom band 1 passes clearColorBlack=true unconditionally") was written 2026-07-29 02:54. The
# mechanism it named was fixed 95 minutes later by two commits to render_geom, and the screens were
# verified rendering. C099 was never falsified, so a week later it read as a current description of
# the renderer and nearly bought a whole native producer as a workaround for what was actually a
# one-line regression somewhere else entirely.
#
# The lever is already in the evidence: C099's evidence NAMED THE CODE IT DEPENDED ON. If a claim
# records that, "has the ground under this moved?" is `git log`. C099 would have been flagged the
# same day, automatically, by a check nobody had to remember to run.
#
# SCOPE IS SYMBOL-LEVEL, NOT FILE-LEVEL, and that is not a nicety — it is what makes the output
# readable. Measured on the real corpus: gpu_vk.cpp took 5 commits in C099's window (only 2 touched
# render_geom), and native_boot.cpp took 2 in C001's window while crt0_setup — the thing C001 is
# actually about — took ZERO. File granularity flags C001, which genuinely still holds. Symbol
# granularity separates them, so the two classes were run, not reasoned about.
FILE_TOK = re.compile(
    r"\b((?:[\w.-]+/)*[\w.+-]+\.(?:cpp|cc|cxx|c|hpp|hh|h|py|sh|rs|go|js|ts|frag|vert|comp|glsl|"
    r"json|cmake|toml|yaml|yml|s|asm|inc))\b")
# Words that look like identifiers but never name code. Kept small on purpose: a bad symbol guess
# costs nothing (it fails `git log -L` validation and we fall back to file scope), a missing one only
# costs a git call.
STOP_SYM = {"the", "and", "for", "with", "from", "that", "this", "which", "when", "then", "into",
            "not", "but", "its", "was", "were", "are", "has", "have", "all", "any", "one", "two",
            "same", "still", "only", "every", "each", "before", "after", "above", "below", "over",
            "under", "because", "since", "while", "does", "did", "run", "runs", "ran", "see",
            "note", "fix", "fixed", "fixes", "claim", "evidence", "commit", "file", "line"}
SYM_TOK = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]{2,}(?:::[A-Za-z_][A-Za-z0-9_]*)*)\b")


def git(repo, *args, check=False):
    """(rc, stdout). Never raises — a missing repo must surface as a REASON, not an exception."""
    try:
        r = subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True, timeout=120)
        return r.returncode, r.stdout
    except Exception as ex:
        return 127, f"{ex}"


def find_repos(root):
    """The superproject plus every submodule that is actually checked out.

    Required, not optional: on this corpus the single most-cited file (gpu_vk.cpp) lives in the
    external/psxport SUBMODULE. `git log` in the superproject sees only pointer bumps, whose dates
    and subjects say nothing about whether the cited function changed. Checking the submodule's own
    history is the only way the real instance is visible at all.
    """
    repos = [root]
    rc, out = git(root, "config", "--file", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    if rc == 0:
        for line in out.splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2:
                p = os.path.join(root, parts[1].strip())
                if os.path.isdir(os.path.join(p, ".git")) or os.path.isfile(os.path.join(p, ".git")):
                    repos.append(p)
    return repos


class Index:
    """basename -> [(repo, relpath)] over every tracked file in every repo, for resolving the bare
    file names claims actually use ("gpu_vk.cpp", not "external/psxport/runtime/recomp/gpu_vk.cpp")."""

    def __init__(self, root):
        self.root, self.repos, self.by_base, self.by_rel, self.files = root, find_repos(root), {}, {}, 0
        self._symcache, self._filetext, self.sym_errors = {}, {}, []
        for repo in self.repos:
            rc, out = git(repo, "ls-files")
            if rc != 0:
                continue
            for rel in out.splitlines():
                if not rel:
                    continue
                self.files += 1
                self.by_base.setdefault(os.path.basename(rel), []).append((repo, rel))
                self.by_rel.setdefault(rel, []).append((repo, rel))

    def resolve(self, token):
        """-> (list of (repo, rel), ambiguous?). Suffix match so 'tools/shot.py' and 'shot.py' both hit."""
        if token in self.by_rel:
            return self.by_rel[token], False
        hits = self.by_base.get(os.path.basename(token), [])
        if "/" in token:
            hits = [h for h in hits if h[1].endswith(token)] or hits
        # A claim's own docs/ tree is not "the code this claim rests on".
        hits = [h for h in hits if not h[1].startswith("docs/")]
        return hits, len(hits) > 1

    def resolve_symbols(self, syms):
        """symbol -> [(repo, rel)] for symbols named with NO file beside them.

        This is what makes migration work rather than merely sound plausible. Measured on the 129
        live claims in the spyro corpus: 49 name a file that resolves, but 104 name an underscored
        identifier, and the archetypal fresh control (C001, "crt0_setup matches psxport's generic
        crt0_setup") names ONLY the symbol — no filename anywhere in its evidence. Without this it is
        invisible; with it, it resolves to native_boot.cpp#crt0_setup and comes back fresh.

        ONE grep per repo over an alternation of every candidate in the corpus, restricted to
        definition-shaped lines. Over-matching is harmless — `git log -L` validation is the real
        filter — but a grep per symbol would cost minutes on a pre-commit gate.
        """
        if LOG_CACHE is not None:                      # 4.4s of the run's ~7s; worth persisting
            for s in syms:
                hit = LOG_CACHE.data.get("S\t" + s)
                if hit is not None and s not in self._symcache:
                    self._symcache[s] = [tuple(x) for x in hit]
        want = sorted(s for s in syms if s not in self._symcache)
        if want:
            for repo in self.repos:
                for i in range(0, len(want), 200):        # keep the pattern under argv/regex limits
                    chunk = want[i:i + 200]
                    # POSIX ERE, not PCRE: `(?:...)` makes git grep exit 128 with "Invalid preceding
                    # regular expression" — which arrives on stderr, so the symbol map came back
                    # EMPTY and every symbol-only claim silently stayed in CANNOT SEE. Caught only
                    # because the coverage number did not move; a checker whose own resolver fails
                    # closed would have reported a smaller, quieter blind spot than it really had.
                    alt = "|".join(re.escape(s) for s in chunk)
                    pat = rf"^[A-Za-z_].*\b({alt})[ \t]*\(|^[ \t]*(def|fn|func|sub)[ \t]+({alt})\b"
                    rc, out = git(repo, "grep", "-l", "-E", pat)
                    if rc not in (0, 1):
                        # A resolver that fails CLOSED reports a smaller blind spot than the truth.
                        # Surface it; never absorb it.
                        self.sym_errors.append(f"{os.path.relpath(repo, self.root)}: git grep rc={rc}")
                        continue
                    for rel in out.splitlines():
                        if not rel or rel.startswith("docs/"):
                            continue
                        for s in chunk:
                            if s in self._files_containing(repo, rel):
                                self._symcache.setdefault(s, []).append((repo, rel))
            for s in want:
                self._symcache.setdefault(s, [])
                if LOG_CACHE is not None:
                    LOG_CACHE.data["S\t" + s] = [list(x) for x in self._symcache[s]]
                    LOG_CACHE.dirty = True
        return {s: self._symcache.get(s, []) for s in syms}

    def _files_containing(self, repo, rel):
        key = (repo, rel)
        if key not in self._filetext:
            try:
                with open(os.path.join(repo, rel), errors="replace") as f:
                    self._filetext[key] = f.read()
            except OSError:
                self._filetext[key] = ""
        return self._filetext[key]


class Dep:
    def __init__(self, repo, rel, sym, origin):
        self.repo, self.rel, self.sym, self.origin = repo, rel, sym, origin
        self.scope = "file"

    def label(self):
        r = os.path.relpath(self.repo, ROOT)
        return f"{'' if r == '.' else r + '/'}{self.rel}" + (f"#{self.sym}" if self.sym else "")


LOG_CACHE = None    # set per-run by analyse_claims; None => uncached (selftest, ad-hoc use)


def _cached(key, produce):
    return LOG_CACHE.get(key, produce) if LOG_CACHE is not None else produce()


def sym_log(repo, rel, sym):
    """Commits touching the LINE RANGE of `sym` in `rel`. (None) when git cannot resolve the range —
    a declaration-only match, a macro, a renamed symbol — in which case the caller MUST fall back to
    file scope rather than reporting a clean 0, which would be a false negative dressed as a pass."""
    def run():
        rc, out = git(repo, "log", f"-L:{sym}:{rel}", "-s", "--format=%ct%x09%h%x09%ci%x09%s")
        return None if rc != 0 else [l for l in out.splitlines() if l.strip()]
    return _cached(f"L\t{repo}\t{rel}\t{sym}", run)


def file_log(repo, rel):
    def run():
        rc, out = git(repo, "log", "--format=%ct%x09%h%x09%ci%x09%s", "--", rel)
        return [l for l in out.splitlines() if l.strip()] if rc == 0 else []
    return _cached(f"F\t{repo}\t{rel}", run)


class LogCache:
    """Memoises `git log -L` across runs. Without it a 129-claim corpus costs ~20s, which is the
    difference between a gate that runs on every commit and one that gets commented out.

    Keyed on every repo's HEAD, and thrown away wholesale when any of them moves — a cache that could
    outlive the history it summarises would manufacture the false FRESH this tool exists to prevent.
    Lives in the user's cache dir, never in the repo: it is a derived artefact and machine-local.
    """

    def __init__(self, repos, root):
        self.path, self.data, self.dirty = None, {}, False
        heads = {}
        for r in repos:
            rc, out = git(r, "rev-parse", "HEAD")
            heads[os.path.relpath(r, root)] = out.strip() if rc == 0 else "?"
        self.heads = heads
        base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.environ.get("HOME", ""), ".cache")
        if not os.environ.get("HOME") and not os.environ.get("XDG_CACHE_HOME"):
            return
        d = os.path.join(base, "project-info")
        try:
            os.makedirs(d, exist_ok=True)
            import hashlib
            self.path = os.path.join(d, hashlib.sha1(os.path.abspath(root).encode()).hexdigest() + ".json")
            with open(self.path) as f:
                blob = json.load(f)
            if blob.get("heads") == heads:
                self.data = blob.get("entries", {})
        except Exception:
            self.data = {}

    def get(self, key, produce):
        if key not in self.data:
            self.data[key] = produce()
            self.dirty = True
        return self.data[key]

    def save(self):
        if not (self.path and self.dirty):
            return
        try:
            with open(self.path, "w") as f:
                json.dump({"heads": self.heads, "entries": self.data}, f)
        except Exception:
            pass


def dep_commits(dep, since_epoch, cache):
    """Commits to this dependency strictly after `since_epoch`. Sets dep.scope as a side effect so
    the report can say WHICH granularity produced the answer — a symbol-scoped 0 and a file-scoped 0
    are different strengths of evidence and must not print the same."""
    key = (dep.repo, dep.rel, dep.sym)
    if key not in cache:
        lines = sym_log(dep.repo, dep.rel, dep.sym) if dep.sym else None
        cache[key] = (lines, "symbol") if lines is not None else (file_log(dep.repo, dep.rel), "file")
    lines, dep.scope = cache[key]
    out = []
    for l in lines:
        p = l.split("\t", 3)
        if len(p) == 4 and int(p[0]) > since_epoch:
            out.append((p[1], p[2], p[3]))
    return out


def parse_depends(val, index):
    deps, unresolved = [], []
    for item in re.split(r"[,\s]+", val or ""):
        if not item.strip():
            continue
        path, _, sym = item.partition("#")
        hits, _amb = index.resolve(path)
        if not hits:
            unresolved.append(item)
            continue
        for repo, rel in hits[:3]:
            deps.append(Dep(repo, rel, sym or None, "declared"))
    return deps, unresolved


BARE_SYM = re.compile(r"\b([A-Za-z_][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)\b")


def bare_symbols(body):
    """Underscored identifiers — the shape function names take in this corpus (crt0_setup,
    render_geom, func_80016500, cd_queue_push). Deliberately narrow: CamelCase and single words
    produce far too many prose false hits to be worth the greps."""
    return {s for s in BARE_SYM.findall(body)
            if len(s) > 5 and s.lower() not in STOP_SYM and not s.endswith("_")}


def infer_depends(body, index, symmap=None):
    """Mine the evidence prose of the ~149 pre-existing claims for the code they name.

    Migration is the whole game: a design that only works for NEW claims fixes nothing, because the
    claim that cost the day was written a week before the checker existed. This is best-effort and
    the report says so — every claim it cannot mine is counted in CANNOT SEE, never in 'fresh'.
    """
    deps, unresolved, seen = [], [], set()
    for m in FILE_TOK.finditer(body):
        tok = m.group(1)
        hits, _amb = index.resolve(tok)
        if not hits:
            unresolved.append(tok)
            continue
        # Symbols are taken from a window around the mention: claims write "gpu_vk.cpp render_geom
        # draws three bands", so the function name sits next to the file name. Capped at 4 — each
        # candidate costs a `git log -L`, and a wrong guess is free (it fails validation).
        lo, hi = max(0, m.start() - 160), min(len(body), m.end() + 160)
        win = body[lo:hi]
        cands = [s for s in SYM_TOK.findall(win)
                 if s.lower() not in STOP_SYM and ("_" in s or "::" in s) and s not in tok][:4]
        for repo, rel in hits[:3]:
            got = False
            for s in cands:
                if (repo, rel, s) in seen:
                    continue
                if sym_log(repo, rel, s) is not None:   # git could resolve the range -> real symbol
                    seen.add((repo, rel, s))
                    deps.append(Dep(repo, rel, s, "inferred"))
                    got = True
            if not got and (repo, rel, None) not in seen:
                seen.add((repo, rel, None))
                deps.append(Dep(repo, rel, None, "inferred"))
    if symmap:
        # Symbols named WITHOUT a file beside them, added ALWAYS — not only when no file was found.
        # Measured consequence of the "only if empty" version: C104, whose subject IS render_geom's
        # early return, mentions ppm_look.py in its evidence, so the file matched, the symbol pass was
        # skipped, and the claim came back FRESH while the function it is about had been rewritten.
        # A false FRESH is the exact failure this tool exists to prevent, so it outranks extra noise.
        for s in sorted(bare_symbols(body)):
            for repo, rel in symmap.get(s, [])[:2]:
                if (repo, rel, s) in seen:
                    continue
                if sym_log(repo, rel, s) is not None:
                    seen.add((repo, rel, s))
                    deps.append(Dep(repo, rel, s, "inferred-symbol"))
    return deps, unresolved


def claim_baseline(e, root, add_cache):
    """(epoch, human description of WHERE the baseline came from).

    Default anchor is the commit that ADDED the claim file, not its `created:` date, and that
    distinction is load-bearing: a claim is almost always committed alongside the very code change it
    documents, so a date-granularity baseline flags every claim against its own commit. Anchoring to
    the add-commit +1s means only genuinely LATER work counts. `verified_at:`/`reconfirmed:` override.
    """
    rel = os.path.relpath(e["_path"], root)
    if rel not in add_cache:
        rc, out = git(root, "log", "--diff-filter=A", "--format=%ct", "--follow", "--", rel)
        adds = [int(x) for x in out.splitlines() if x.strip().isdigit()] if rc == 0 else []
        add_cache[rel] = min(adds) if adds else None
    base, why = add_cache[rel], None
    if base is not None:
        base, why = base + 1, "commit that added the claim"
    for k in ("verified_at", "reconfirmed", "created"):
        v = (e.get(k) or "").strip()
        if not v:
            continue
        try:
            d = datetime.datetime.fromisoformat(v[:10])
        except ValueError:
            continue
        ep = int(d.timestamp())
        if k in ("verified_at", "reconfirmed"):   # explicit re-verification always wins
            return ep, f"{k}: {v}"
        if base is None:                          # untracked claim file: fall back to created:
            return ep, f"created: {v} (claim file is UNTRACKED — baseline is coarse)"
    if base is None:
        return None, "NO BASELINE (untracked and no usable date)"
    return base, why


def analyse_claims(root=None, use_cache=True):
    """-> (rows, index, reason). rows: one dict per holds-claim. Never invents a clean result."""
    root = root or ROOT
    claims_dir = os.path.join(root, "docs", "info", "claims")
    if not os.path.isdir(claims_dir):
        return None, None, f"docs/info/claims does not exist under {root}"
    files = [f for f in sorted(os.listdir(claims_dir)) if f.endswith(".md")]
    if not files:
        return None, None, f"{claims_dir} contains no claim files"
    global LOG_CACHE
    index = Index(root)
    # The selftest runs UNCACHED on purpose: a self-test that can be satisfied from a cache is
    # testing the cache, not the detector it is supposed to keep honest.
    LOG_CACHE = LogCache(index.repos, root) if use_cache else None
    live = [e for e in entries(claims_dir) if e.get("status") == "holds" and not e.get("depends")]
    symmap = index.resolve_symbols({s for e in live for s in bare_symbols(e["_body"])})
    cache, add_cache, rows = {}, {}, []
    for e in entries(claims_dir):
        row = {"id": e.get("id", "?"), "status": e.get("status", "?"), "path": e["_path"],
               "title": next((l for l in e["_body"].splitlines()
                              if l.strip() and not l.startswith("#")), "")[:88]}
        rows.append(row)
        if row["status"] != "holds":
            row["kind"] = "skipped"       # already dead; the ledger is not lying about it
            continue
        deps, unresolved = ([], [])
        if e.get("depends"):
            deps, unresolved = parse_depends(e["depends"], index)
            origin = "declared"
        else:
            deps, unresolved = infer_depends(e["_body"], index, symmap)
            origin = "inferred"
        row["origin"], row["unresolved"] = origin, unresolved
        base, why = claim_baseline(e, root, add_cache)
        row["baseline"], row["baseline_why"] = base, why
        if not deps:
            row["kind"] = "blind"
            row["why_blind"] = ("evidence names files this repo does not have: "
                                + ", ".join(sorted(set(unresolved))[:4]) if unresolved
                                else "evidence names no source file at all")
            continue
        if base is None:
            row["kind"] = "blind"
            row["why_blind"] = "no usable verification baseline"
            continue
        row["deps"] = deps
        changed = []
        for d in deps:
            for sha, when, subj in dep_commits(d, base, cache):
                changed.append((d, sha, when, subj))
        row["changed"] = changed
        row["kind"] = "stale" if changed else "fresh"
    if LOG_CACHE is not None:
        LOG_CACHE.save()
    return rows, index, None


def cmd_claim_check(a):
    if getattr(a, "selftest", False):
        return selftest()
    rows, index, reason = analyse_claims()
    if rows is None:
        # REFUSE. "No stale claims, exit 0" over a corpus that does not exist is the exact lie this
        # whole check was built to stop: it is indistinguishable from having looked and found nothing.
        print(f"info: CHECKED NOTHING — {reason}.\n"
              f"      This is a REFUSAL, not a pass. There is no evidence here that any claim is fresh.",
              file=sys.stderr)
        return 2
    tot = len(rows)
    skipped = [r for r in rows if r["kind"] == "skipped"]
    blind = [r for r in rows if r["kind"] == "blind"]
    fresh = [r for r in rows if r["kind"] == "fresh"]
    stale = [r for r in rows if r["kind"] == "stale"]
    checked = fresh + stale
    decl = len([r for r in checked if r.get("origin") == "declared"])

    print(f"== CLAIM STALENESS  ({index.files} tracked files across {len(index.repos)} repo(s): "
          f"{', '.join(os.path.relpath(p, ROOT) for p in index.repos)})\n")
    for r in stale:
        print(f"STALE  {r['id']}  {r['title']}")
        print(f"       verified at: {r['baseline_why']}   deps: {r.get('origin')}")
        by = {}
        for d, sha, when, subj in r["changed"]:
            by.setdefault(d.label() + f"  [{d.scope}-scope]", []).append((sha, when, subj))
        for lbl, cs in by.items():
            print(f"       {lbl} — {len(cs)} commit(s) since:")
            for sha, when, subj in cs[:3]:
                print(f"         {sha} {when[:10]} {subj[:78]}")
        print(f"       -> re-verify, then `info.py claim confirm {r['id']} --evidence '...'` "
              f"or `info.py claim falsify {r['id']} --why '...'`\n")

    if a.verbose:
        for r in fresh:
            print("fresh  {}  {}".format(r["id"], ", ".join(
                f"{d.label()}[{d.scope}]" for d in r["deps"][:4])))

    # THE DENOMINATOR. Printed on EVERY run, including (especially) the all-clear one. "0 stale" on
    # its own is indistinguishable from "never looked", and this tool exists because a silent
    # negative already cost a day.
    print(f"  claims on file .............. {tot}")
    print(f"  not checked (not 'holds') ... {len(skipped)}  (falsified/other — already marked dead)")
    print(f"  CHECKED ..................... {len(checked)}  "
          f"({decl} declared `depends:`, {len(checked) - decl} inferred from evidence prose)")
    print(f"     stale (code moved) ....... {len(stale)}")
    print(f"     fresh .................... {len(fresh)}")
    print(f"  *** CANNOT SEE .............. {len(blind)} ***  these record NO resolvable code "
          f"dependency.")
    if blind:
        print(f"      This check is BLIND to them. They are NOT fresh — they are UNCHECKED, and a "
              f"'0 stale' above\n      says nothing whatsoever about them. Fix by re-running "
              f"`claim add --depends path.cpp#symbol`\n      or adding a `depends:` line to the "
              f"frontmatter of:")
        for r in blind[:12]:
            print(f"        {r['id']}  ({r['why_blind']})")
        if len(blind) > 12:
            print(f"        ... and {len(blind) - 12} more (`--verbose` lists all)")
        if a.verbose:
            for r in blind[12:]:
                print(f"        {r['id']}  ({r['why_blind']})")
    print(f"\n  Blind spots this check CANNOT cover even for checked claims: evidence that rests on "
          f"data\n  (a ROM, an asset, a recorded trace), on a tool's behaviour, or on code in a repo "
          f"not indexed\n  above. Symbol scope also cannot see a change made to a CALLER that "
          f"invalidates the callee.")
    if index.sym_errors:
        print(f"\n  RESOLVER FAILED on {len(index.sym_errors)} query/queries "
              f"({'; '.join(sorted(set(index.sym_errors))[:3])}) — the CANNOT SEE count above is an "
              f"UNDER-count and the checked count is an over-count.")
    if stale:
        return 1
    if blind and a.strict:
        print("\n  --strict: exiting non-zero because unchecked claims exist.")
        return 1
    return 0


# ------------------------------------------------------------- selftest
SELFTEST_STALE = "STALE"
SELFTEST_FRESH = "FRESH"


def selftest():
    """Run the detector against BOTH classes, in the shipping artifact.

    A discriminator that was reasoned about but never run against a case that MUST come back positive
    AND one that MUST come back negative is exactly the failure this repo keeps hitting — one scored
    25 on the negative case and 0 on the positive. So: a fixture repo, a claim whose cited FUNCTION is
    edited after it was written (must be STALE), a claim whose cited function is untouched while its
    FILE changes around it (must be FRESH — this is the case file-granularity gets wrong), a claim
    naming no code (must be CANNOT SEE, not fresh), and an empty corpus (must REFUSE with exit 2).
    """
    global ROOT, CLAIMS, INSTR
    import shutil
    base = os.environ.get("CLAUDE_SCRATCH") or os.path.join(ROOT, "scratch")
    work = os.path.join(base, "info_selftest")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "src"))
    cl = os.path.join(work, "docs", "info", "claims")
    os.makedirs(cl)

    def g(*args, **kw):
        rc, out = git(work, *args)
        if rc != 0:
            raise SystemExit(f"selftest: git {' '.join(args)} failed:\n{out}")

    def w(rel, text):
        with open(os.path.join(work, rel), "w") as f:
            f.write(text)

    def commit(msg, when):
        env = {**os.environ, "GIT_AUTHOR_DATE": when, "GIT_COMMITTER_DATE": when,
               "GIT_AUTHOR_NAME": "selftest", "GIT_AUTHOR_EMAIL": "s@t", "GIT_COMMITTER_NAME":
               "selftest", "GIT_COMMITTER_EMAIL": "s@t"}
        r = subprocess.run(["git", "commit", "-q", "-m", msg], cwd=work, env=env,
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f"selftest: commit failed: {r.stdout}{r.stderr}")

    g("init", "-q", "-b", "main")
    g("config", "user.email", "s@t")
    g("config", "user.name", "selftest")
    w("src/render.cpp", "void render_geom(){ clear(true); }\nvoid other(){ int x=1; }\n")
    w("src/boot.cpp", "void crt0_setup(){ sp=1; }\nvoid unrelated(){ int y=2; }\n")
    g("add", "-A"); commit("initial", "2020-01-01T00:00:00")

    w("docs/info/claims/001-stale-case.md",
      "---\nid: C001\nkind: claim\nstatus: holds\ncreated: 2020-01-02\n---\n\n## Claim\n\n"
      "The screen is black by design.\n\n## Evidence\n\nrender.cpp render_geom passes clear(true) "
      "unconditionally.\n")
    w("docs/info/claims/002-fresh-case.md",
      "---\nid: C002\nkind: claim\nstatus: holds\ncreated: 2020-01-02\n---\n\n## Claim\n\n"
      "The boot entry matches the generic setup.\n\n## Evidence\n\nboot.cpp crt0_setup assigns sp "
      "field for field.\n")
    w("docs/info/claims/003-no-dependency.md",
      "---\nid: C003\nkind: claim\nstatus: holds\ncreated: 2020-01-02\n---\n\n## Claim\n\n"
      "The artwork looks right.\n\n## Evidence\n\nI looked at the screenshot and it looked fine.\n")
    g("add", "-A"); commit("claims", "2020-01-02T00:00:00")

    # AFTER both claims: edit the function C001 names, and edit boot.cpp WITHOUT touching the
    # function C002 names. File granularity would call C002 stale; symbol granularity must not.
    w("src/render.cpp", "void render_geom(){ if(!preserve) clear(true); }\nvoid other(){ int x=1; }\n")
    w("src/boot.cpp", "void crt0_setup(){ sp=1; }\nvoid unrelated(){ int y=2; int z=3; }\n")
    g("add", "-A"); commit("later work", "2020-02-01T00:00:00")

    ROOT_SAVE, C_SAVE, I_SAVE = ROOT, CLAIMS, INSTR
    fails = []
    try:
        ROOT = work
        CLAIMS, INSTR = cl, os.path.join(work, "docs", "info", "instruments")
        rows, index, reason = analyse_claims(work, use_cache=False)
        if rows is None:
            raise SystemExit(f"selftest: analyse refused on a valid corpus: {reason}")
        kind = {r["id"]: r["kind"] for r in rows}
        detail = {r["id"]: r for r in rows}
        print(f"  positive case C001 (cited function edited)      -> {kind.get('C001')}")
        print(f"  negative case C002 (file edited, function not)  -> {kind.get('C002')}")
        print(f"  no-dependency C003 (evidence names no code)     -> {kind.get('C003')}")
        if kind.get("C001") != "stale":
            fails.append(f"C001 MUST be stale, got {kind.get('C001')} — the detector cannot see a "
                         f"change to the code a claim names; every 'fresh' it reports is worthless")
        if kind.get("C002") != "fresh":
            fails.append(f"C002 MUST be fresh, got {kind.get('C002')} — the detector flags unrelated "
                         f"churn in the same file; it will be muted as noise within a day")
        if kind.get("C003") != "blind":
            fails.append(f"C003 MUST be blind, got {kind.get('C003')} — a claim resting on nothing "
                         f"checkable is being laundered into a result")
        if kind.get("C001") == "stale":
            scopes = {d.scope for d, *_ in detail["C001"]["changed"]}
            if "symbol" not in scopes:
                fails.append(f"C001 flagged at {scopes} scope, not symbol — the symbol path is dead "
                             f"and C002's pass is luck, not discrimination")
        # REFUSAL case: an empty corpus must never come back clean.
        empty = os.path.join(work, "empty")
        os.makedirs(os.path.join(empty, "docs", "info", "claims"), exist_ok=True)
        r2, _, reason2 = analyse_claims(empty, use_cache=False)
        print(f"  empty corpus                                   -> "
              f"{'REFUSED: ' + reason2 if r2 is None else 'RETURNED ROWS'}")
        if r2 is not None:
            fails.append("an EMPTY claims dir returned a result instead of refusing — 'no stale "
                         "claims' would be printed over a corpus that was never looked at")
        r3, _, reason3 = analyse_claims(os.path.join(work, "does-not-exist"), use_cache=False)
        if r3 is not None:
            fails.append("a MISSING claims dir returned a result instead of refusing")
    finally:
        ROOT, CLAIMS, INSTR = ROOT_SAVE, C_SAVE, I_SAVE
    if fails:
        print("\nSELFTEST FAILED:")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("\n  selftest PASSED: positive flagged, negative not flagged, blind counted separately, "
          "empty corpus refused.")
    shutil.rmtree(work, ignore_errors=True)
    return 0


# ----------------------------------------------------------- instruments
def cmd_instr_add(a):
    ensure(INSTR)
    iid, n = next_id(INSTR, "I")
    p = os.path.join(INSTR, f"{n:03d}-{slug(a.tool)}.md")
    write(p, {"id": iid, "kind": "instrument", "status": "trusted", "created": today()},
          f"## Instrument\n\n{a.tool}\n\n## Validated by\n\n{a.validated}\n\n"
          f"## Known failure modes\n\n(none recorded yet)\n")
    print(f"{iid}  {p}")


def cmd_instr_distrust(a):
    e = by_id(INSTR, a.id)
    fr, bo = parse(e["_path"])
    fr["status"] = "DISTRUSTED"
    fr["distrusted_on"] = today()
    write(e["_path"], fr, bo + f"\n\n## DISTRUSTED {today()}\n\n{a.why}\n\n"
          f"> Every result this instrument produced is suspect until it is re-validated.\n")
    print(f"{e.get('id')} marked DISTRUSTED — re-check results that used it")


def cmd_instr_list(a):
    for e in entries(INSTR):
        st = e.get("status", "?")
        if a.distrusted and st != "DISTRUSTED":
            continue
        mark = "✓" if st == "trusted" else "✗"
        body = e["_body"].split(chr(10))
        print(f"{mark} {e.get('id')}  [{st}]  {body[2][:90] if len(body)>2 else ''}")


# ----------------------------------------------------------------- brief
def _grep_dir(d, words, label, limit=6):
    """Returns (shown, total_entries) so the caller can tell "no match" from "empty registry"."""
    if not os.path.isdir(d):
        return 0, 0
    hits = []
    for e in entries(d):
        blob = (e["_body"] + " " + " ".join(e.values() if False else [])).lower()
        score = sum(blob.count(w.lower()) for w in words)
        if score:
            hits.append((score, e))
    shown = sorted(hits, key=lambda x: -x[0])[:limit]
    for _, e in shown:
        first = [l for l in e["_body"].splitlines() if l.strip() and not l.startswith("#")]
        print(f"    [{e.get('status','?')}] {e.get('id')}  {(first[0] if first else '')[:88]}")
    return len(shown), len(list(entries(d)))


def _run(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=ROOT)
        return r.stdout.strip()
    except Exception:
        return ""


def cmd_brief(a):
    # Split INSIDE each argument. `brief <words>` is nargs='+', so the natural
    # `info.py brief "native render tev lighting"` arrives as ONE term and matches nothing —
    # then the footer says "genuinely new ground" and the caller believes the registries are
    # empty when they are not. That is the worst failure mode this tool has: it is consulted
    # exactly once, at task start, and a false negative there costs the whole session.
    words = [w for arg in a.words for w in str(arg).split()]
    q = " ".join(words)
    print(f"== INFORMATION BRIEF: {q}\n")
    print("  CLAIMS (has this been 'proven' before — and does it still hold?)")
    nc, tc = _grep_dir(CLAIMS, words, "claims")
    if nc:
        # `holds` means "nobody has falsified it", NOT "still true". C099 read [holds] here a week
        # after the mechanism it described had been fixed, and that reading nearly bought a whole
        # native producer as a workaround. The staleness test is not run inline (cold it costs ~10s,
        # and brief must stay instant) — but the caller is told it exists, every time.
        print("    ^ [holds] means UNCHALLENGED, not CURRENT. Before citing one of these as fact, "
              "run `info.py claim check`\n      — it reports whether the code the claim's evidence "
              "names has changed since it was verified.")
    print("\n  INSTRUMENTS (can the tool you're about to trust actually show the other answer?)")
    ni, ti = _grep_dir(INSTR, words, "instruments")
    cat = os.path.join(SKILLS, "issue-catalog", "catalog.py")
    issues_dir = os.path.join(ROOT, "docs", "issues")
    if os.path.exists(cat) and os.path.isdir(issues_dir):
        # ONE POSITIONAL, NOT MANY. catalog.py's search takes a single query
        # argument; passing the split words made argparse reject the call with
        # "unrecognized arguments", which goes to stderr and leaves stdout
        # EMPTY -- so this whole section was silently skipped for every
        # multi-word brief. Which is every real one. A catalog with dozens of
        # entries was never consulted by the command that exists to consult it.
        out = _run([sys.executable, cat, "search", q])
        if out:
            print("\n  ISSUES (symptom -> cause / dead end)")
            print(textwrap.indent("\n".join(out.splitlines()[:8]), "    "))
        else:
            # A miss here is a real miss; say so with the denominator rather
            # than printing nothing, which reads identically to "not consulted".
            n = len([f for f in os.listdir(issues_dir) if f.endswith(".md")])
            print(f"\n  ISSUES: no match across {n} entr(ies) in docs/issues")
    elif os.path.exists(cat):
        print("\n  ISSUES: docs/issues does not exist here, so the issue "
              "catalog was NOT consulted -- this is not an empty result")
    for name, tool, data in (("CODEMAP", "codemap/codemap.py", "docs/code-map.md"),
                             ("RE-FRONTIER", "re-frontier/re_frontier.py", "docs/re-frontier.md")):
        if os.path.exists(os.path.join(ROOT, data)):
            hits = _run(["git", "grep", "-in", "-m", "3", q, "--", data])
            if hits:
                print(f"\n  {name}")
                print(textwrap.indent("\n".join(hits.splitlines()[:4]), "    "))
    # project-local trackers (every project grows its own; surface them rather than ignore them)
    local = [t for t in ("tools/kanban.py", "tools/findings.py") if os.path.exists(os.path.join(ROOT, t))]
    if local:
        print(f"\n  PROJECT-LOCAL TRACKERS present: {', '.join(local)} — query them too.")
    # Only claim "new ground" when the registries really are empty. Saying it while 5 claims and
    # 7 instruments sit unmatched tells the next session to go and re-derive them.
    if nc or ni:
        return
    if tc or ti:
        print(f"\n  No match for those terms, but the registries are NOT empty "
              f"({tc} claim(s), {ti} instrument(s)). Try fewer or different words, or "
              f"`info.py claim list` / `info.py instrument list`.")
    elif not (os.path.isdir(CLAIMS) or os.path.isdir(INSTR)):
        # MISSING IS NOT EMPTY. Saying "genuinely new ground" because the
        # ledgers do not exist yet tells the caller that nothing is known, when
        # the truth is that nothing has ever been RECORDED here -- a different
        # statement with a different action attached.
        print("\n  THE CLAIMS AND INSTRUMENTS LEDGERS DO NOT EXIST IN THIS "
              "PROJECT YET, so they were not searched. This is NOT evidence "
              "that the ground is new. Create them with `info.py claim add` / "
              "`info.py instrument add`, and note that any ISSUES/CODEMAP/"
              "RE-FRONTIER sections above are the only registries that were "
              "actually consulted.")
    else:
        print("\n  (nothing above ⇒ genuinely new ground; record what you learn with "
              "`info.py claim add`)")


# A claim recorded as "FALSIFIES C0xx" only does its job if the claim it kills is actually MARKED dead.
# Recording the killer and forgetting to flip the victim leaves a known-dead claim reading [holds] in
# every future brief — which is strictly worse than never having recorded it, because it now carries
# the ledger's authority. This happened: C017 ("FALSIFIES C016") was filed while C016 stayed [holds],
# and C016 went on being served as a live result for the rest of the session.
# Direction matters and the two phrasings mean OPPOSITE things: "this FALSIFIES C016" names a victim,
# while "FALSIFIED BY C017" names a killer of the claim you are reading. A single pattern for both
# reported C016 as falsifying C017 — its own falsify note said "Falsified by C017" — so they are split.
KILLS_RE  = re.compile(r"\bfalsifies\s+(C\d+)", re.I)          # this claim kills <victim>
KILLED_RE = re.compile(r"\bfalsified by\s+(C\d+)", re.I)       # this claim is killed by <killer>


def cmd_check(a):
    claims = entries(CLAIMS)
    status = {e.get("id"): e.get("status") for e in claims}
    bad = [e for e in entries(INSTR) if e.get("status") == "DISTRUSTED"]
    fal = [e for e in claims if e.get("status") == "falsified"]
    for e in bad:
        print(f"DISTRUSTED INSTRUMENT {e.get('id')} — results using it are suspect")
    for e in fal:
        print(f"FALSIFIED CLAIM {e.get('id')} — anything citing it needs re-checking")

    problems = 0
    # (a) a claim declares it falsifies another, but that other one still reads as holding
    for e in claims:
        if e.get("status") != "holds":
            continue
        body = e.get("_body", "")
        for victim in KILLS_RE.findall(body):
            if status.get(victim.upper()) == "holds":
                problems += 1
                print(f"INCONSISTENT: {e.get('id')} says it falsifies {victim.upper()}, but "
                      f"{victim.upper()} still reads [holds] — run: info.py claim falsify "
                      f"{victim.upper()} --why '...'")
        for killer in KILLED_RE.findall(body):
            problems += 1
            print(f"INCONSISTENT: {e.get('id')} says it is falsified by {killer.upper()}, yet "
                  f"{e.get('id')} itself still reads [holds] — run: info.py claim falsify "
                  f"{e.get('id')} --why '...'")
    # (b) a claim with no stated falsifier is a belief, not a result (CLAIMS ledger rule)
    nofals = [e.get("id") for e in claims
              if e.get("status") == "holds" and "## What would falsify it" in e.get("_body", "")
              and not e.get("_body", "").split("## What would falsify it", 1)[1].strip()]
    for cid in nofals:
        problems += 1
        print(f"NO FALSIFIER: {cid} states no observation that would disprove it — that is a belief, "
              f"not a result")

    if not bad and not fal and not problems:
        print("info: no distrusted instruments, no falsified claims, ledger self-consistent")

    # Staleness is folded in here so a pre-commit gate gets it for free. Summary only — `claim check`
    # prints the per-claim detail. The counts (including CANNOT SEE) are printed unconditionally:
    # a gate that says nothing on the clean path teaches the reader that silence means "verified".
    if not a.no_stale:
        rows, index, reason = analyse_claims()
        if rows is None:
            print(f"STALENESS: CHECKED NOTHING — {reason}. Not a pass.")
            problems += 1
        else:
            st = [r for r in rows if r["kind"] == "stale"]
            bl = [r for r in rows if r["kind"] == "blind"]
            ck = [r for r in rows if r["kind"] in ("stale", "fresh")]
            for r in st:
                print(f"STALE CLAIM {r['id']} — code it cites moved since it was verified "
                      f"({len(r['changed'])} commit(s)); run `info.py claim check` for detail")
            print(f"STALENESS: checked {len(ck)} of {len(rows)} claim(s); {len(st)} stale, "
                  f"{len(ck) - len(st)} fresh, {len(bl)} record NO code dependency and are "
                  f"NOT covered by this check")
            problems += len(st)
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description="project information system")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("brief", help="ONE query across every registry — run this at task start")
    b.add_argument("words", nargs="+")
    b.set_defaults(fn=cmd_brief)

    c = sub.add_parser("claim").add_subparsers(dest="c", required=True)
    ca = c.add_parser("add"); ca.add_argument("claim"); ca.add_argument("--evidence", required=True)
    ca.add_argument("--tag", action="append")
    # --falsified-by is the real name; --expires-on is kept as an alias because the first draft used it.
    # TWO independent agents misread "expires-on" as a DATE field (one passed a date, one thought no such
    # flag existed and left the falsifier UNSPECIFIED) — the value is a CONDITION, not a time.
    ca.add_argument("--falsified-by", "--expires-on", dest="expires_on", metavar="CONDITION",
                    help="what OBSERVATION would disprove this claim (not a date) — e.g. "
                         "'if the reference renderer shares the fault, this number proves nothing'")
    ca.add_argument("--depends", action="append", metavar="PATH[#SYMBOL]",
                    help="the code this claim's evidence rests on, e.g. "
                         "runtime/recomp/gpu_vk.cpp#render_geom. Makes rot mechanically detectable "
                         "by `claim check`; without it the claim is INVISIBLE to that check.")
    ca.set_defaults(fn=cmd_claim_add)
    cl = c.add_parser("list"); cl.add_argument("--stale", action="store_true")
    cl.add_argument("--falsified", action="store_true"); cl.set_defaults(fn=cmd_claim_list)
    cs = c.add_parser("check", help="has the code a claim rests on changed since it was verified?")
    cs.add_argument("--strict", action="store_true", help="also fail when claims are unchecked")
    cs.add_argument("--verbose", action="store_true")
    cs.add_argument("--selftest", action="store_true",
                    help="run the detector against a case that MUST be stale and one that MUST be fresh")
    cs.set_defaults(fn=cmd_claim_check)
    cf = c.add_parser("falsify"); cf.add_argument("id"); cf.add_argument("--why", required=True)
    cf.set_defaults(fn=cmd_claim_falsify)
    cc = c.add_parser("confirm"); cc.add_argument("id"); cc.add_argument("--evidence", required=True)
    cc.add_argument("--depends", action="append", metavar="PATH[#SYMBOL]")
    cc.set_defaults(fn=cmd_claim_confirm)

    i = sub.add_parser("instrument").add_subparsers(dest="i", required=True)
    ia = i.add_parser("add"); ia.add_argument("tool"); ia.add_argument("--validated", required=True)
    ia.set_defaults(fn=cmd_instr_add)
    idd = i.add_parser("distrust"); idd.add_argument("id"); idd.add_argument("--why", required=True)
    idd.set_defaults(fn=cmd_instr_distrust)
    il = i.add_parser("list"); il.add_argument("--distrusted", action="store_true")
    il.set_defaults(fn=cmd_instr_list)

    ck = sub.add_parser("check")
    ck.add_argument("--no-stale", action="store_true", help="skip the claim-staleness pass")
    ck.set_defaults(fn=cmd_check)

    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
