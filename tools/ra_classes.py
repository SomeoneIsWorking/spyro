#!/usr/bin/env python3
"""Audit every `jr $ra` the recompiler emitted as a COMPUTED JUMP instead of a return.

WHY THIS EXISTS. psxport's `ra_computed_jumps` (emit.py) decides, per `jr $ra`, whether the value
being jumped through is a caller's return address (emit `return;`) or a mid-body continuation (emit
`rec_dispatch(c, ra)`). Get it wrong in the COMPUTED direction and a perfectly ordinary return is
dispatched to an address that is not a function entry -> `recomp-MISS` -> SIGABRT, thousands of
frames into a run, with a backtrace that points at the CALLER and says nothing about the emitter.
That is docs/issues/0040, and it has now happened to this port twice.

The analysis is a STATIC SET QUESTION, so it must not need a 15-minute recompile + a 90-second run
to answer. This script answers it in about a second, off the shipped `generated/` set.

WHAT IT CHECKS. Two independent PROOFS that a site is an ordinary return. Neither is a heuristic and
neither has a threshold to tune; a site survives only if BOTH fail to prove it.

  RULE A — GLOBAL SAVE SLOT (partition-free).
    Spyro's hand-written renderers have no stack frame. They spill `$ra` to a FIXED GLOBAL save
    block -- `sw $ra, 44($at)` with `$at` materialised as 0x80077DD8 -- and reload it in the
    epilogue. `ra_computed_jumps` calls `lw $ra, N(rX)` a save-slot reload only when some
    `sw $ra, N(...)` sits in the SAME partition entry, and its partition is the emitted function
    list, not the guest function. A global block is shared across guest functions by construction,
    so that test answers "continuation" on a plain return. Rule A resolves the load's base to a
    link-time constant and looks for the matching store MODULE-WIDE.

  RULE B — REACHING DEFINITIONS OVER THE CFG.
    `ra_computed_jumps` documents itself as "reaching-definitions on $ra" but implements a LINEAR
    SWEEP: it walks addresses in order and calls the last definition it passed the reaching one.
    A `jal` on a branch path that does not reach the `jr` therefore poisons it. 0x80053570 is
    exactly that: `bne` at 0x8005358C skips the `jal` at 0x80053598, and the two `jr $ra` at
    0x800535E0 / 0x80053600 are reachable ONLY through the skip. Rule B does the real thing --
    a forward fixpoint over the basic-block graph, merging disagreeing paths to UNKNOWN, which the
    emitter's own stated default ("anything it cannot prove -> return") renders as a return.

NEGATIVE DESIGN. A zero-length audit set is reported as a REFUSAL, not a pass: "no computed sites"
is also what a missing/stale `generated/` looks like. Every verdict prints its denominator. And
`--selftest` runs BOTH rules against a case that MUST come out COMPUTED and a case that MUST come
out RETURN, because a discriminator that has only been run against one class is not a discriminator
-- this project has been bitten by exactly that.

BLIND SPOTS, stated because a green result must not read as "the emitter is correct":
  * It audits MAIN only. The overlay modules are emitted by separate `emit_module` calls with their
    own partitions and are NOT covered here.
  * Rule A resolves a base only through a local `lui`/`addiu` pair within 16 instructions. A base
    arriving in an argument register, or computed further back, is invisible and reports UNRESOLVED
    rather than being silently treated as either answer.
  * It says nothing about `jr` through a register other than `$ra`, nor about whether the emitted
    body is correct for any other reason.

    python3 tools/ra_classes.py [--selftest]

Exit: 0 = every computed site survived both proofs (a genuine coroutine set), 1 = at least one is a
misclassified ordinary return, 2 = the script could not measure anything and says so.
"""
import bisect
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "external/psxport/tools/recomp"))

import psexe                      # noqa: E402
import decode as D                # noqa: E402
from decode import decode         # noqa: E402
import emit                       # noqa: E402

EXE = os.path.join(REPO, "scratch/bin/spyro/SCUS_942.28")
DECLS = os.path.join(REPO, "generated/rec_decls.h")


def die(msg):
    print(f"REFUSED: {msg}", file=sys.stderr)
    sys.exit(2)


# ─────────────────────────────────────────────────────────────────────────────── the two proofs ──

def resolve_const_base(exe, at, reg):
    """The link-time constant address in `reg` at `at`, or None.

    Only the `lui rX,H` / `addiu rX,rX,L` idiom, scanned back 16 instructions. Anything else is
    UNRESOLVED and reported as such -- never guessed in either direction."""
    lo_add = None
    for x in range(at - 4, max(at - 68, exe.load) - 4, -4):
        i = decode(x, exe.word(x))
        if i.op == "addiu" and i.rt == reg and lo_add is None and i.rs == reg:
            lo_add = i.simm
        elif i.op == "lui" and i.rt == reg:
            return ((i.simm & 0xFFFF) << 16) + (lo_add or 0)
        elif i.rt == reg and i.kind in (D.LOAD, D.ALU_RRI) and i.op != "addiu":
            return None
        elif i.rd == reg and i.kind in (D.ALU_RRR, D.SHIFT_I, D.SHIFT_V):
            return None
    return None


def ra_save_slots(exe):
    """Every `sw $ra, N(base)` in the module, keyed by (const base or None, N).

    This is the module-wide denominator Rule A reports: how many stores it had to look at."""
    slots = {}
    for a in range(exe.load, exe.text_end, 4):
        i = decode(a, exe.word(a))
        if i.op == "sw" and i.rt == 31:
            base = None if i.rs == 29 else resolve_const_base(exe, a, i.rs)
            slots.setdefault((base, i.simm), []).append(a)
    return slots


def ra_transfer(i):
    """`ra_computed_jumps`' own per-instruction rule, kept identical on purpose: this script must
    reproduce the emitter's classification before it may disagree with it."""
    if i.op == "lw" and i.rt == 31:
        return ("return" if i.rs == 29 else "load", i)
    if i.op in ("jal", "bltzal", "bgezal"):
        return ("computed", i)
    if i.kind == D.JUMPR and i.op == "jalr" and i.rd == 31:
        return ("computed", i)
    if (i.kind in (D.ALU_RRR, D.SHIFT_I, D.SHIFT_V) and i.rd == 31) \
            or (i.kind in (D.ALU_RRI, D.LUI) and i.rt == 31):
        return ("return", i)
    return (None, None)


def linear_reaching_def(exe, lo, hi, site):
    """What the SHIPPED analysis believes reaches `site`: last definition in address order."""
    cur = None
    for a in range(lo, hi, 4):
        if a == site:
            return cur
        st, i = ra_transfer(decode(a, exe.word(a)))
        if st is not None:
            cur = (st, a, i)
    return cur


def cfg_reaching_defs(exe, lo, hi, site):
    """Rule B. Forward fixpoint over the basic-block graph of [lo,hi); returns the set of states
    with which `site` is actually reachable. Disagreement stays as a set, and the caller applies
    the emitter's stated default (prove it or return)."""
    state = {}                                   # addr -> set of states on entry
    work = [(lo, frozenset({None}))]
    seen = set()
    at_site = set()
    while work:
        a, st = work.pop()
        if not (lo <= a < hi):
            continue
        if (a, st) in seen:
            continue
        seen.add((a, st))
        state.setdefault(a, set()).update(st)
        i = decode(a, exe.word(a))
        if i.kind == D.UNKNOWN:
            continue                             # trailing data, not code
        if a == site:
            at_site.update(st)
            continue                             # `jr` ends the path
        cls, _ = ra_transfer(i)
        nxt = frozenset({cls}) if cls is not None else st
        if i.kind in (D.BRANCH, D.JUMP) or i.kind == D.JUMPR:
            # ORDER MATTERS AND IS THE HARDWARE'S: a branch-and-link writes `$ra` when it issues,
            # and the delay-slot instruction executes AFTER that — so a delay slot that also
            # defines `$ra` overrides the link, not the other way round.
            if i.op in ("jal", "bltzal", "bgezal"):
                nxt = frozenset({"computed"})    # the link, established by the call itself
            ds = decode(a + 4, exe.word(a + 4))
            dcls, _ = ra_transfer(ds)
            nxt = frozenset({dcls}) if dcls is not None else nxt
            if i.kind == D.JUMPR:
                if i.op == "jalr":
                    work.append((a + 8, nxt))
                continue                         # an unresolved register jump ends this path
            if i.target is not None:
                work.append((i.target, nxt))
            if i.kind == D.BRANCH or i.op in ("jal", "bltzal", "bgezal"):
                work.append((a + 8, nxt))        # conditional / call: fall through past the slot
            continue
        work.append((a + 4, nxt))
    return at_site


# ─────────────────────────────────────────────────────────────────────────────────── the report ──

def audit(exe, funcs, computed, slots, out=sys.stdout):
    """Verdict per site. Returns the list of sites proven to be ordinary returns."""
    fs = sorted(funcs)
    misclassified = []
    for site in sorted(computed):
        k = bisect.bisect_right(fs, site) - 1
        f_lo = fs[k]
        f_hi = fs[k + 1] if k + 1 < len(fs) else exe.text_end
        rd = linear_reaching_def(exe, f_lo, f_hi, site)
        print(f"\n  jr $ra @ 0x{site:08X}   emitted fragment [0x{f_lo:08X},0x{f_hi:08X})", file=out)
        verdicts = []

        # RULE A
        if rd and rd[0] == "load":
            i = rd[2]
            base = resolve_const_base(exe, rd[1], i.rs)
            if base is None:
                print(f"    A  the reaching load 0x{rd[1]:08X} `lw $ra,{i.simm}(r{i.rs})` has an "
                      f"UNRESOLVED base — Rule A cannot see this site, neither way.", file=out)
            else:
                stores = slots.get((base, i.simm), [])
                where = " ".join(f"0x{a:08X}" for a in stores)
                print(f"    A  reaching load 0x{rd[1]:08X} `lw $ra,{i.simm}(0x{base:08X})`; "
                      f"module-wide `sw $ra,{i.simm}(0x{base:08X})`: {len(stores)} "
                      f"[{where or 'none'}]  (searched {sum(len(v) for v in slots.values())} "
                      f"`sw $ra` sites in [0x{exe.load:08X},0x{exe.text_end:08X}))", file=out)
                if stores:
                    verdicts.append("A: it is a SAVE SLOT — ordinary return")
        elif rd:
            print(f"    A  reaching def is `{rd[2].op}` @ 0x{rd[1]:08X}, not a load — "
                  f"Rule A does not apply.", file=out)
        else:
            print("    A  no reaching definition in the fragment — Rule A does not apply.", file=out)

        # RULE B
        states = cfg_reaching_defs(exe, f_lo, f_hi, site)
        pretty = "{" + ", ".join(sorted(str(s) for s in states)) + "}"
        print(f"    B  states reaching the `jr` over the CFG of the fragment: {pretty}", file=out)
        if states and states != {"computed"}:
            verdicts.append("B: not `computed` on every path — the emitter's own default is return")
        elif not states:
            print("       (the `jr` is not reachable from the fragment's first instruction — the "
                  "fragment start is not this body's entry, so Rule B has no opinion.)", file=out)

        if verdicts:
            misclassified.append(site)
            for v in verdicts:
                print(f"    -> MISCLASSIFIED. {v}", file=out)
        else:
            print("    -> survives both proofs: a genuine coroutine resume.", file=out)
    return misclassified


def selftest():
    """Run both rules against a case that MUST be COMPUTED and cases that MUST be RETURN.

    A discriminator scored against one class only is not evidence. Rule B's positive case is
    synthetic because this binary has no genuine coroutine; its negative cases are the REAL
    0x80053570 body and the REAL 0x80022A2C save-slot return, so the negatives are not synthetic
    at all."""
    class Img:
        def __init__(self, load, words):
            self.load = load
            self.words = words
            self.text_end = load + 4 * len(words)

        def word(self, a):
            return self.words[(a - self.load) // 4]

    # POSITIVE: a real coroutine. `jal` to a helper establishes a live link, then `jr $ra` resumes
    # through it on EVERY path. Both rules must leave it COMPUTED.
    #   0x80010000  jal 0x80010010
    #   0x80010004  nop
    #   0x80010008  jr  $ra
    #   0x8001000C  nop
    pos = Img(0x80010000, [0x0C004004, 0x00000000, 0x03E00008, 0x00000000])
    ok = True
    st = cfg_reaching_defs(pos, 0x80010000, pos.text_end, 0x80010008)
    if st != {"computed"}:
        ok = False
        print(f"  SELFTEST FAIL: the synthetic coroutine classified {st}, want {{'computed'}}")
    else:
        print("  selftest +  synthetic coroutine (jal then jr $ra on every path): Rule B says "
              "computed — the rule CAN produce the positive answer.")

    exe = load_exe()
    slots = ra_save_slots(exe)
    # NEGATIVE 1 (real): the path-dead jal at 0x80053570.
    st = cfg_reaching_defs(exe, 0x80053570, 0x80053608, 0x800535E0)
    if st == {"computed"}:
        ok = False
        print(f"  SELFTEST FAIL: 0x800535E0 classified {st} — Rule B did not see the skip path.")
    else:
        print(f"  selftest -  real 0x800535E0 (bne skips the jal): Rule B says {sorted(map(str, st))} "
              "— not computed on every path.")
    # NEGATIVE 2 (real): the global save slot behind 0x80023ABC.
    base = resolve_const_base(exe, 0x80023A8C, 1)
    stores = slots.get((base, 44), [])
    if base != 0x80077DD8 or 0x80022A60 not in stores:
        ok = False
        print(f"  SELFTEST FAIL: Rule A resolved base {base!r} / {len(stores)} stores for "
              "0x80023A8C; expected 0x80077DD8 with the store at 0x80022A60.")
    else:
        print(f"  selftest -  real 0x80023A8C `lw $ra,44(0x{base:08X})`: Rule A finds "
              f"{len(stores)} matching stores module-wide — it is a save slot.")
    return ok


def load_exe():
    if not os.path.exists(EXE):
        die(f"no executable at {EXE} — this script searched NOTHING. "
            f"Run `python3 tools/ensure_recomp.py` to provision it, then re-run.")
    return psexe.load(EXE)


def main():
    if "--selftest" in sys.argv:
        print("ra_classes selftest — both classes, because a rule run against one is not a rule:")
        ok = selftest()
        print("  SELFTEST PASS" if ok else "  SELFTEST FAIL")
        return 0 if ok else 1

    exe = load_exe()
    if not os.path.exists(DECLS):
        die(f"no {DECLS} — the emitted function set is unknown, so this script searched NOTHING.")
    # THE STALENESS QUESTION, ANSWERED MECHANICALLY. This script's partition comes from `generated/`
    # while its classification is recomputed live from the executable, so the two can describe
    # different substrates. `.recomp_version` is the framework's own explicit staleness signal — use
    # it, instead of treating an empty result as proof of staleness (which is what the zero-case
    # refusal below used to do, and which made "0 computed" permanently unreportable once the
    # emitter was fixed and 0 became the TRUE answer for this game).
    vf = os.path.join(REPO, "generated/.recomp_version")
    have = open(vf).read().strip() if os.path.exists(vf) else "(absent)"
    if have != emit.RECOMP_VERSION:
        die(f"generated/ was emitted by recompiler {have!r} but tools/recomp/emit.py is now "
            f"{emit.RECOMP_VERSION!r}. The function partition on disk is not the one this emitter "
            f"produces, so anything measured against it describes a substrate that is not built. "
            f"Re-run `PSXPORT_FORCE_RECOMP=1 python3 tools/ensure_recomp.py` and try again.")
    funcs = set()
    for line in open(DECLS):
        k = line.find("void gen_func_")
        while k >= 0:
            funcs.add(int(line[k + 14:k + 22], 16))
            k = line.find("void gen_func_", k + 1)
    funcs = {f for f in funcs if exe.load <= f < exe.text_end}
    if not funcs:
        die(f"{DECLS} parsed to ZERO in-text MAIN functions — the parse is broken, not the binary.")

    computed = emit.ra_computed_jumps(exe, sorted(funcs))
    slots = ra_save_slots(exe)
    n_jr = sum(1 for a in range(exe.load, exe.text_end, 4)
               if (lambda i: i.op == "jr" and i.rs == 31)(decode(a, exe.word(a))))
    print(f"MAIN [0x{exe.load:08X},0x{exe.text_end:08X}): {len(funcs)} emitted functions, "
          f"{n_jr} `jr $ra` sites, {sum(len(v) for v in slots.values())} `sw $ra` sites.")
    print(f"psxport `ra_computed_jumps` classified {len(computed)} of those {n_jr} `jr $ra` as "
          f"COMPUTED JUMPS (emitted as `rec_dispatch`, not `return`).")
    if not computed:
        # NOT a refusal any more — the staleness check above already ruled out the reading this
        # refusal existed to prevent, and ZERO is the correct answer for this game (Spyro's MAIN has
        # no coroutine `jr $ra` anywhere; see docs/issues/0046). It still prints its denominators,
        # because "0 of 778 over 790 fragments" and "(none)" are not the same statement.
        print(f"\n0 of 778-scale corpus: no `jr $ra` in MAIN is emitted as a computed jump. "
              f"Denominators above are real ({len(funcs)} fragments, {n_jr} `jr $ra` sites, "
              f"{sum(len(v) for v in slots.values())} `sw $ra` sites), and the generated set on disk "
              f"was emitted by this exact recompiler ({emit.RECOMP_VERSION}).")
        print("BLIND SPOTS: MAIN only (overlay modules use their own partitions and are not "
              "audited); Rule A cannot resolve a base that is not a local lui/addiu pair; neither "
              "rule says anything about a `jr` through a register other than $ra.")
        return 0

    bad = audit(exe, funcs, computed, slots)
    print(f"\n{len(bad)} of {len(computed)} computed sites are PROVEN ORDINARY RETURNS: "
          + (" ".join(f"0x{a:08X}" for a in bad) or "(none)"))
    print("BLIND SPOTS: MAIN only (overlay modules use their own partitions and are not audited); "
          "Rule A cannot resolve a base that is not a local lui/addiu pair; neither rule says "
          "anything about a `jr` through a register other than $ra.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
