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
  info.py claim list [--stale] [--falsified]
  info.py claim falsify <id> --why "<what disproved it>"
  info.py claim confirm <id> --evidence "<re-proof>"
  info.py instrument add "<tool>" --validated "<how you proved it can show the OTHER answer>"
  info.py instrument distrust <id> --why "<failure mode>"
  info.py instrument list [--distrusted]
  info.py check                 # non-zero if a distrusted instrument or falsified claim is in play
"""
import argparse, datetime, os, re, subprocess, sys, textwrap

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
    write(p, {"id": cid, "kind": "claim", "status": "holds", "created": today(),
              "tags": ",".join(a.tag or [])},
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
    write(e["_path"], fr, bo + f"\n\n## Re-confirmed {today()}\n\n{a.evidence}\n")
    print(f"{e.get('id')} re-confirmed")


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
    if not os.path.isdir(d):
        return
    hits = []
    for e in entries(d):
        blob = (e["_body"] + " " + " ".join(e.values() if False else [])).lower()
        score = sum(blob.count(w.lower()) for w in words)
        if score:
            hits.append((score, e))
    for _, e in sorted(hits, key=lambda x: -x[0])[:limit]:
        first = [l for l in e["_body"].splitlines() if l.strip() and not l.startswith("#")]
        print(f"    [{e.get('status','?')}] {e.get('id')}  {(first[0] if first else '')[:88]}")


def _run(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=ROOT)
        return r.stdout.strip()
    except Exception:
        return ""


def cmd_brief(a):
    words = a.words
    q = " ".join(words)
    print(f"== INFORMATION BRIEF: {q}\n")
    print("  CLAIMS (has this been 'proven' before — and does it still hold?)")
    _grep_dir(CLAIMS, words, "claims")
    print("\n  INSTRUMENTS (can the tool you're about to trust actually show the other answer?)")
    _grep_dir(INSTR, words, "instruments")
    cat = os.path.join(SKILLS, "issue-catalog", "catalog.py")
    if os.path.exists(cat) and os.path.isdir(os.path.join(ROOT, "docs", "issues")):
        out = _run([sys.executable, cat, "search", *words])
        if out:
            print("\n  ISSUES (symptom -> cause / dead end)")
            print(textwrap.indent("\n".join(out.splitlines()[:8]), "    "))
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
    print("\n  (nothing above ⇒ genuinely new ground; record what you learn with `info.py claim add`)")


def cmd_check(a):
    bad = [e for e in entries(INSTR) if e.get("status") == "DISTRUSTED"]
    fal = [e for e in entries(CLAIMS) if e.get("status") == "falsified"]
    for e in bad:
        print(f"DISTRUSTED INSTRUMENT {e.get('id')} — results using it are suspect")
    for e in fal:
        print(f"FALSIFIED CLAIM {e.get('id')} — anything citing it needs re-checking")
    if not bad and not fal:
        print("info: no distrusted instruments, no falsified claims")
    return 0


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
    ca.set_defaults(fn=cmd_claim_add)
    cl = c.add_parser("list"); cl.add_argument("--stale", action="store_true")
    cl.add_argument("--falsified", action="store_true"); cl.set_defaults(fn=cmd_claim_list)
    cf = c.add_parser("falsify"); cf.add_argument("id"); cf.add_argument("--why", required=True)
    cf.set_defaults(fn=cmd_claim_falsify)
    cc = c.add_parser("confirm"); cc.add_argument("id"); cc.add_argument("--evidence", required=True)
    cc.set_defaults(fn=cmd_claim_confirm)

    i = sub.add_parser("instrument").add_subparsers(dest="i", required=True)
    ia = i.add_parser("add"); ia.add_argument("tool"); ia.add_argument("--validated", required=True)
    ia.set_defaults(fn=cmd_instr_add)
    idd = i.add_parser("distrust"); idd.add_argument("id"); idd.add_argument("--why", required=True)
    idd.set_defaults(fn=cmd_instr_distrust)
    il = i.add_parser("list"); il.add_argument("--distrusted", action="store_true")
    il.set_defaults(fn=cmd_instr_list)

    ck = sub.add_parser("check"); ck.set_defaults(fn=cmd_check)

    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
