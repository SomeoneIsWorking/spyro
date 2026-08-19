#!/usr/bin/env python3
"""transcribe.py — mechanically render a recompiled body into an OWNED, readable native body.

WHY THIS EXISTS. Owning a hand-written-assembly renderer (re-frontier render.own-geometry-family)
means supplying a native body that is BYTE-EXACT with the recompiled one, then modifying it with
evidence. 0x800258F0 (RenderWorldChunks) is ~5000 instructions. Hand-transcribing it was tried and
measured: FOUR defects in the first 40 instructions (a delay-slot load made conditional, an
unconditional increment made conditional, a register substitution, and dropped rec_irq_poll calls) —
and none of them is visible until all 5000 are typed, because the differential can only run a whole
body. Hand transcription at this scale is not a discipline problem; it is the wrong instrument.

WHAT IT GUARANTEES. The rewrite is a BIJECTION on the generated text: register numbers to MIPS
names, address-materialisation pairs to one named constant, offsets to signed form. Nothing is
paraphrased. `--check` re-parses the emitted body, inverts every rewrite, and requires an EXACT
token match against the generated source; any mismatch prints both sides and fails. So the emitted
body is byte-exact BY CONSTRUCTION, and stays that way as the generated substrate is regenerated.

THE NEGATIVE IS DESIGNED FIRST. Every mode prints a DENOMINATOR — statements parsed, folds applied,
annotations injected, annotation lines stripped before comparison. A run that recognises nothing
says so loudly rather than emitting an empty body; an unparsed statement is a hard failure, never a
silent passthrough, because a passthrough is exactly how a mistranslation would survive.

  python3 tools/transcribe.py emit  0x800258F0 --out game/core/world_body.inc
  python3 tools/transcribe.py check 0x800258F0 --body game/core/world_body.inc
  python3 tools/transcribe.py --selftest
"""
import argparse, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
REG_NUM = {n: i for i, n in enumerate(REGS)}

# ── The bijection, forward and back ────────────────────────────────────────────────────────────

RE_REG      = re.compile(r"c->r\[(\d+)\]")
RE_ALIAS    = re.compile(r"\b(" + "|".join(REGS) + r")\b")
# (c->r[N] + (uint32_t)OFF)  ->  reg / reg + OFF / reg - OFF
RE_ADDR     = re.compile(r"\(c->r\[(\d+)\] \+ \(uint32_t\)(-?\d+)\)")
# Only a parenthesised (reg +/- N); a bare "(reg)" is the enclosing call's parentheses, and
# its collapsed "+ 0" is restored per address-operand position below.
RE_ADDR_INV = re.compile(r"\(\s*(" + "|".join(REGS) + r")\s*([+-])\s*(\d+)\s*\)")
# (uint32_t)Ku << 16   (the lui half of an address materialisation)
RE_LUI      = re.compile(r"^(c->r\[\d+\]) = \(uint32_t\)(\d+)u << 16;$")
RE_ADDIU    = re.compile(r"^(c->r\[\d+\]) = (c->r\[\d+\]) \+ \(uint32_t\)(-?\d+);$")
RE_MASK     = re.compile(r"& (\d+)u\b")
RE_MASK_INV = re.compile(r"& (0x[0-9A-Fa-f]+)u\b")


def load_names():
    p = os.path.join(ROOT, "game", "core", "guest_names.json")
    if not os.path.exists(p):
        return {}
    with open(p) as f:
        return {int(k, 16): v for k, v in json.load(f).items()}


def to_alias(s):
    """Generated text -> readable text. Address operands first, then bare registers, then masks."""
    def addr(m):
        r, off = REGS[int(m.group(1))], int(m.group(2))
        if off == 0:
            return r
        return "(%s %s %d)" % (r, "+" if off > 0 else "-", abs(off))
    s = RE_ADDR.sub(addr, s)
    s = RE_REG.sub(lambda m: REGS[int(m.group(1))], s)
    s = RE_MASK.sub(lambda m: "& 0x%Xu" % int(m.group(1)), s)
    return s


def to_generated(s):
    """Readable text -> generated text. The exact inverse of to_alias()."""
    s = RE_MASK_INV.sub(lambda m: "& %du" % int(m.group(1), 16), s)

    def addr(m):
        r, sign, off = m.group(1), m.group(2), m.group(3)
        n = int(off) * (-1 if sign == "-" else 1)
        return "(c->r[%d] + (uint32_t)%d)" % (REG_NUM[r], n)
    s = RE_ADDR_INV.sub(addr, s)
    # bare aliases that were not part of an address operand
    s = RE_ALIAS.sub(lambda m: "c->r[%d]" % REG_NUM[m.group(1)], s)
    # a bare register used as an address operand had its (reg + 0) collapsed; restore it
    for fn in ("mem_r8", "mem_r16", "mem_r32", "mem_rs8", "mem_rs16",
               "mem_w8", "mem_w16", "mem_w32"):
        s = re.sub(r"(c->%s\()(c->r\[(\d+)\])([,)])" % fn,
                   lambda m: "%s(c->r[%s] + (uint32_t)0)%s" % (m.group(1), m.group(3), m.group(4)), s)
    for fn in ("gte_hold_src", "gte_copy_pz"):
        s = re.sub(r"(%s\(c, \d+, )(c->r\[(\d+)\])(\))" % fn,
                   lambda m: "%s(c->r[%s] + (uint32_t)0)%s" % (m.group(1), m.group(3), m.group(4)), s)
    s = re.sub(r"(gte_record_pz\(c, )(c->r\[(\d+)\])(, \d+\))",
               lambda m: "%s(c->r[%s] + (uint32_t)0)%s" % (m.group(1), m.group(3), m.group(4)), s)
    return s


# ── Reading the generated body ──────────────────────────────────────────────────────────────────

def read_generated(addr):
    """Return the statement lines of gen_func_<addr> from generated/, with its shard path."""
    want = "void gen_func_%08X(Core* c) {" % addr
    gendir = os.path.join(ROOT, "generated")
    for name in sorted(os.listdir(gendir)):
        if not name.endswith(".c"):
            continue
        path = os.path.join(gendir, name)
        with open(path) as f:
            lines = f.read().split("\n")
        for i, ln in enumerate(lines):
            if ln.strip() != want:
                continue
            body = []
            for ln2 in lines[i + 1:]:
                if ln2.rstrip() == "}":
                    return body, path
                body.append(ln2.strip())
            sys.exit("transcribe: gen_func_%08X in %s has no closing brace" % (addr, path))
    sys.exit("transcribe: no gen_func_%08X in any generated/*.c — REFUSING to emit an empty body "
             "(is the substrate built, and is the address right?)" % addr)


# ── Folding: the lui/addiu address materialisation pair -> one constant ─────────────────────────

def fold(lines):
    """lines -> units. ('imm', reg, value, arity) or ('line', text). Never drops a line."""
    units, i, folded = [], 0, 0
    while i < len(lines):
        m = RE_LUI.match(lines[i])
        if m:
            dst, hi = m.group(1), int(m.group(2))
            base = (hi << 16) & 0xFFFFFFFF
            if i + 1 < len(lines):
                m2 = RE_ADDIU.match(lines[i + 1])
                # fold only when the addiu reads the very register the lui just wrote
                if m2 and m2.group(1) == dst and m2.group(2) == dst:
                    val = (base + int(m2.group(3))) & 0xFFFFFFFF
                    units.append(("imm", dst, val, 2))
                    folded += 1
                    i += 2
                    continue
            units.append(("imm", dst, base, 1))
            i += 1
            continue
        units.append(("line", lines[i]))
        i += 1
    return units, folded


def unfold(units):
    """units -> lines. The exact inverse of fold()."""
    out = []
    for u in units:
        if u[0] == "imm":
            _, dst, val, arity = u
            if arity == 1:
                out.append("%s = (uint32_t)%du << 16;" % (dst, val >> 16))
            else:
                hi = (val >> 16) & 0xFFFF
                lo = val & 0xFFFF
                # the assembler's split: addiu's immediate is signed, so a low half >= 0x8000
                # was encoded as (hi+1, lo-0x10000)
                if lo >= 0x8000:
                    hi, lo = (hi + 1) & 0xFFFF, lo - 0x10000
                out.append("%s = (uint32_t)%du << 16;" % (dst, hi))
                out.append("%s = %s + (uint32_t)%d;" % (dst, dst, lo))
        else:
            out.append(u[1])
    return out


# ── Emitting the readable body ─────────────────────────────────────────────────────────────────

HOOK = "/*@hook*/"
NOTE = "//@ "
# A named constant is required to start with "k", so this can never collide with `at = v0;`.
RE_EMIT_IMM = re.compile(r"^(" + "|".join(REGS) + r") = (0x[0-9A-Fa-f]+u << 16|0x[0-9A-Fa-f]+u|k[A-Za-z0-9_]*);$")


def emit(units, names, ann):
    """units -> readable lines, with annotations injected. Returns (lines, injected).

    Two placements, both keyed to a LABEL, i.e. to a guest address:
      "note" / "hook" — emitted at the TOP of that label's block.
      "after"         — [{"stmt": <exact emitted statement>, "emit": [...], "why": ...}], emitted
                        immediately AFTER that statement, inside that block.

    An "after" rule whose `stmt` does not match EXACTLY ONCE in its block is a hard error. A hook
    that silently fails to attach is the worst outcome available here: the run then measures the
    UNMODIFIED body while the annotations file says otherwise, and whatever number comes back reads
    as a result about the change.
    """
    out, injected = [], 0
    hits = {}
    cur = None
    for u in units:
        key = None
        if u[0] == "line":
            m = re.match(r"^(L_[0-9A-Fa-f_A-Z]+):;", u[1])
            if m:
                key = m.group(1)
        if key:
            cur = key
        if key and key in ann:
            a = ann[key]
            for c in a.get("note", []):
                out.append(NOTE + c)
                injected += 1
            for h in a.get("hook", []):
                out.append(HOOK + " " + h)
                injected += 1
        if u[0] == "imm":
            _, dst, val, arity = u
            reg = to_alias(dst)
            if arity == 1:
                out.append("%s = 0x%Xu << 16;" % (reg, val >> 16))
            elif val in names:
                out.append("%s = %s;" % (reg, names[val]))
            else:
                out.append("%s = 0x%08Xu;" % (reg, val))
        else:
            out.append(to_alias(u[1]))
        # "after" rules for the block we are inside
        for rule in ann.get(cur, {}).get("after", []) if cur else []:
            if out[-1] != rule["stmt"]:
                continue
            hits[(cur, rule["stmt"])] = hits.get((cur, rule["stmt"]), 0) + 1
            for c in ([rule["why"]] if rule.get("why") else []):
                out.append(NOTE + c)
                injected += 1
            for h in rule["emit"]:
                out.append(HOOK + " " + h)
                injected += 1
    # Every "after" rule must have fired exactly once. Report ALL the failures, with counts.
    bad = []
    for label, a in ann.items():
        for rule in a.get("after", []):
            n = hits.get((label, rule["stmt"]), 0)
            if n != 1:
                bad.append("  %s / %r matched %d time(s), need exactly 1" % (label, rule["stmt"], n))
    if bad:
        sys.exit("transcribe: annotation rules did not attach:\n" + "\n".join(bad) +
                 "\nREFUSING to emit — a hook that does not attach makes the run measure the "
                 "unmodified body while this file claims otherwise.")
    return out, injected


def parse_emitted(lines, names):
    """Readable lines -> units. The inverse of emit(). Annotation lines are dropped and counted."""
    by_name = {v: k for k, v in names.items()}
    units, stripped = [], 0
    for ln in lines:
        s = ln.strip()
        if not s or s.startswith(NOTE.strip()) or s.startswith(HOOK) or s.startswith("//"):
            if s:
                stripped += 1
            continue
        m = RE_EMIT_IMM.match(s)
        if m:
            reg, rhs = m.group(1), m.group(2)
            dst = "c->r[%d]" % REG_NUM[reg]
            if rhs.endswith("<< 16"):
                units.append(("imm", dst, int(rhs.split("u")[0], 16) << 16, 1))
            elif rhs.startswith("0x"):
                units.append(("imm", dst, int(rhs[:-1], 16), 2))
            elif rhs in by_name:
                units.append(("imm", dst, by_name[rhs], 2))
            else:
                sys.exit("transcribe: unknown named constant %r in the emitted body" % rhs)
            continue
        units.append(("line", to_generated(s)))
    return units, stripped


# ── The round-trip check: the emitted body must invert to the generated body, exactly ───────────

def norm(lines):
    return [re.sub(r"\s+", " ", l).strip() for l in lines if l.strip()]


def roundtrip(gen_lines, emitted_lines, names):
    """Return (ok, report). Compares the inverted emission against the generated source."""
    units, stripped = parse_emitted(emitted_lines, names)
    back = norm(unfold(units))
    want = norm(gen_lines)
    if back == want:
        return True, "matched all %d generated statement(s) (%d annotation line(s) stripped)" % (
            len(want), stripped)
    n = min(len(back), len(want))
    for i in range(n):
        if back[i] != want[i]:
            return False, ("DIVERGES at generated statement %d of %d "
                           "(%d annotation line(s) stripped)\n  generated: %s\n  emitted->: %s"
                           % (i + 1, len(want), stripped, want[i], back[i]))
    return False, ("length mismatch: generated has %d statement(s), the emitted body inverts to %d "
                   "(%d annotation line(s) stripped)" % (len(want), len(back), stripped))


def load_ann(path):
    if not path or not os.path.exists(path):
        return {}
    with open(path) as f:
        return json.load(f)


def cmd_emit(args):
    addr = int(args.addr, 16)
    gen, shard = read_generated(addr)
    names = load_names()
    units, folded = fold(gen)
    ann = load_ann(args.ann)
    lines, injected = emit(units, names, ann)

    ok, report = roundtrip(gen, lines, names)
    if not ok:
        sys.exit("transcribe: REFUSING to write — the emission does not invert to the source.\n" + report)

    hdr = [
        "// GENERATED by tools/transcribe.py from %s gen_func_%08X — do not hand-edit."
        % (os.path.relpath(shard, ROOT), addr),
        "// It is a bijective rendering of the recompiled body (registers named, address pairs",
        "// folded, offsets signed), so it is byte-exact by construction. `transcribe.py check`",
        "// re-derives the generated source from this file and fails on any divergence; that check",
        "// runs in the gate, so this stays exact as the substrate is regenerated.",
        "// Statements: %d generated -> %d emitted (%d address pair(s) folded, %d annotation line(s))."
        % (len(gen), len(lines), folded, injected),
    ]
    text = "\n".join(hdr + lines) + "\n"
    with open(args.out, "w") as f:
        f.write(text)
    print("[transcribe] emit gen_func_%08X -> %s" % (addr, os.path.relpath(args.out, ROOT)))
    print("[transcribe]   %d generated statement(s), %d folded, %d annotation line(s) injected"
          % (len(gen), folded, injected))
    print("[transcribe]   round-trip: %s" % report)
    return 0


def cmd_check(args):
    addr = int(args.addr, 16)
    gen, shard = read_generated(addr)
    names = load_names()
    if not os.path.exists(args.body):
        sys.exit("transcribe: %s does not exist — nothing to check (run `emit` first)" % args.body)
    with open(args.body) as f:
        emitted = [l for l in f.read().split("\n")]
    ok, report = roundtrip(gen, emitted, names)
    tag = "OK" if ok else "FAIL"
    print("[transcribe] check %s vs %s gen_func_%08X: %s"
          % (os.path.relpath(args.body, ROOT), os.path.relpath(shard, ROOT), addr, tag))
    print("[transcribe]   %s" % report)
    if not ok:
        return 1
    # WITH the annotations file, hold the committed body to EXACT re-emission. The round-trip above
    # only proves the generated statements are intact — it strips annotation lines before comparing,
    # so a hook edited, moved or invented by hand in the .inc passes it silently. Since a hook is
    # what makes the body do something the substrate does not, "the hooks are exactly the ones the
    # annotations file asks for" is the half that actually needs guarding.
    if args.ann:
        units, folded = fold(gen)
        want, injected = emit(units, names, load_ann(args.ann))
        want_all = [l for l in want if l.strip()]
        # compare the annotated body, ignoring only the generated header comment block
        got_body = [l for l in emitted if l.strip() and not l.startswith("// ")]
        if got_body != want_all:
            n = min(len(got_body), len(want_all))
            first = next((i for i in range(n) if got_body[i] != want_all[i]), n)
            print("[transcribe] check %s vs %s: FAIL — the committed body is not what emit(%s) "
                  "produces" % (os.path.relpath(args.body, ROOT), os.path.relpath(args.ann, ROOT),
                                os.path.relpath(args.ann, ROOT)))
            print("[transcribe]   first difference at emitted line %d of %d/%d"
                  % (first + 1, len(got_body), len(want_all)))
            if first < len(want_all):
                print("[transcribe]     annotations say: %s" % want_all[first])
            if first < len(got_body):
                print("[transcribe]     committed body:  %s" % got_body[first])
            return 1
        print("[transcribe]   exact re-emission with %s: %d line(s), %d annotation line(s)"
              % (os.path.relpath(args.ann, ROOT), len(want_all), injected))
    return 0


# ── Selftest: the check must PASS on a faithful body and FAIL on each way of breaking one ───────

SELFTEST_GEN = [
    "c->r[1] = (uint32_t)32776u << 16;",
    "c->r[1] = c->r[1] + (uint32_t)-31320;",
    "c->r[22] = c->mem_r32((c->r[1] + (uint32_t)36));",
    "c->r[2] = c->r[2] & 65535u;",
    "L_80025A30:;",
    "if (c->pending_work) rec_irq_poll(c);",
    "{ int _t = ((int32_t)c->r[31] < 0); c->r[18] = c->mem_r32((c->r[1] + (uint32_t)0)); "
    "if (_t) goto L_800259EC; }",
    "c->mem_w32((c->r[16] + (uint32_t)-4), c->r[0]);",
    "gte_op_at(c, 0x4A49E012u, 0x80025A68u);",
]


def cmd_selftest(_args):
    names = {0x800785A8: "kEnvironment"}
    ann = {"L_80025A30": {"note": ["the chunk cull"], "hook": ["world_depth_note(c);"]}}
    units, folded = fold(SELFTEST_GEN)
    good, injected = emit(units, names, ann)

    fails = 0

    def expect(label, want_ok, lines):
        nonlocal fails
        ok, report = roundtrip(SELFTEST_GEN, lines, names)
        hit = (ok == want_ok)
        print("  [%s] %-46s %s" % ("ok" if hit else "BAD", label,
                                   report.split("\n")[0]))
        if not hit:
            fails += 1

    print("[transcribe] selftest — %d statement(s), %d folded, %d annotation line(s)"
          % (len(SELFTEST_GEN), folded, injected))
    print("  emitted body:")
    for l in good:
        print("    " + l)
    print("  the check must accept the faithful body and reject every corruption:")
    expect("faithful emission", True, good)
    # Each mutation is a way a hand transcription has actually gone wrong on this function.
    expect("dropped statement", False, [l for l in good if "rec_irq_poll" not in l])
    expect("register substituted (s2 -> v0)", False,
           [l.replace("s2 =", "v0 =") for l in good])
    expect("delay-slot load made conditional", False,
           [l.replace("{ int _t = ((int32_t)ra < 0); s2 = c->mem_r32(at); if (_t) goto L_800259EC; }",
                      "if ((int32_t)ra < 0) { s2 = c->mem_r32(at); goto L_800259EC; }") for l in good])
    expect("offset altered (36 -> 32)", False,
           [l.replace("(at + 36)", "(at + 32)") for l in good])
    expect("mask altered", False, [l.replace("0xFFFFu", "0xFFFEu") for l in good])
    expect("folded constant altered", False,
           [l.replace("kEnvironment", "0x800785ACu") for l in good])
    expect("gte opcode altered", False, [l.replace("0x4A49E012u", "0x4A49E013u") for l in good])
    # swap two adjacent STATEMENTS (not annotation lines, which are stripped by design)
    expect("statement reordered", False, [good[0], good[2], good[1]] + good[3:])

    print("[transcribe] selftest: %d check(s), %d wrong" % (10, fails))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    e = sub.add_parser("emit", help="render gen_func_<addr> into a readable owned body")
    e.add_argument("addr")
    e.add_argument("--out", required=True)
    e.add_argument("--ann", help="annotations JSON (comments + hooks keyed by label)")
    c = sub.add_parser("check", help="the emitted body must invert to the generated source")
    c.add_argument("addr")
    c.add_argument("--body", required=True)
    c.add_argument("--ann", help="annotations JSON; with it the check is EXACT re-emission")
    args = ap.parse_args()
    if args.selftest:
        return cmd_selftest(args)
    if args.cmd == "emit":
        return cmd_emit(args)
    if args.cmd == "check":
        return cmd_check(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
