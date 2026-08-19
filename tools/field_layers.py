#!/usr/bin/env python3
"""field_layers.py — what does each layer of the FIELD (stage 0) arm actually DRAW?

WHY. `game/render/scene.cpp` carries the field arm's 10-layer list as the native-renderer backlog,
and six of its ten entries said "(role not RE'd)". Sizing that backlog by reading ten bodies by hand
is exactly the re-derivation this repo keeps paying for, and the obvious shortcut — "follow the call
graph to a known renderer" — gives the WRONG answer on this game, because two of the ten layers are
hand-written assembly renderers that project and emit INLINE and therefore call nothing.

WHAT IT MEASURES. For each layer, the DIRECT-call closure (jal/j only), and over that whole closure
the count of COP2 instructions (COP2 / LWC2 / SWC2) — the GTE traffic that a layer doing 3D
projection cannot avoid and a layer doing 2D or pure logic does not have. The layer's OWN body is
counted separately, which is what catches the inline renderers the closure walk misses.

WHAT A NEGATIVE PRINTS. Every layer prints its closure size, the number of instructions SCANNED, the
cop2 total, its own body's share, and the top contributing functions. So "cop2=0" reads as
"scanned 459 instructions in 10 functions and found none", never as "nothing was looked at". A layer
whose closure is 1 function and 0 instructions is reported as a REFUSAL, not as "does no 3D".

WHAT IT CANNOT SEE, stated because a negative here is not a proof: `jalr` / function-pointer edges
are invisible to the closure walk (callgraph.py's own caveat). A layer that dispatches through a
table can do arbitrary 3D work that this tool scores 0. The own-body count is the partial guard; a
0 for a layer with a large own body and no closure is a "look again", not an answer.

    python3 tools/field_layers.py                 # the census
    python3 tools/field_layers.py --targets       # also: which known submitters each layer reaches
"""
import argparse
import importlib.util
import os
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _load_callgraph():
    """callgraph.py is a script, not a module; load it without running its main()."""
    path = os.path.join(ROOT, "tools", "callgraph.py")
    spec = importlib.util.spec_from_file_location("callgraph_mod", path)
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["callgraph.py"]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


# The stage-0 arm of the render driver 0x8001ED5C, in the guest's own draw order. This list is the
# SAME one game/render/scene.cpp ships; it is repeated here rather than parsed out of the .cpp
# because this tool is what the .cpp's roles were derived FROM, and a tool that read its answer out
# of the file it justifies would certify nothing.
LAYERS = [
    (0x800521C0, "moby list build"),
    (0x80019300, "collectables"),
    (0x80018908, "demo-mode text"),
    (0x80019698, "actor pass"),
    (0x8002B9CC, "environment / world"),
    (0x80050BD0, "cyclorama / sky"),
    (0x800573C8, "particles"),
    (0x800190D4, "screen fade"),
    (0x80018F30, "screen border"),
    (0x800189F0, "tracers"),
]

# Known geometry submitters, for --targets. Names are what THIS repo has proven (C147 mute map,
# corroborated by the vendored decomp), never invented.
TARGETS = {
    0x800258F0: "RenderWorldChunks (ground + cliffs)",
    0x8004EBA8: "EmitStaticActorMeshList (sky + distant terrain)",
    0x8004F000: "EmitStaticActorMeshListFogged",
    0x8001F158: "moby renderer init / culling",
    0x8001F798: "EmitActorDrawList (the character)",
    0x80020F34: "EmitSecondaryActorPrimitives (a second character)",
    0x80022A2C: "RasterizeSpritePrimQueue (sprites)",
    0x80023AC4: "paired actor (has a native producer)",
    0x800580F4: "sprite / billboard emitter",
    0x800168DC: "front-list link (the shared AddPrim leaf)",
}


def function_extents(exe):
    """entry -> (lo, hi), splitting on `jr ra` + delay slot — callgraph.edges()'s own convention."""
    starts, prev_jr = [exe.load], False
    for a in range(exe.load, exe.text_end - 4, 4):
        w = exe.word(a)
        if prev_jr:
            starts.append(a + 4)
            prev_jr = False
        if w == 0x03E00008:
            prev_jr = True
    starts = sorted(set(starts))
    return {s: (s, starts[i + 1] if i + 1 < len(starts) else exe.text_end)
            for i, s in enumerate(starts)}


def cop2_of(exe, ext, fn):
    """(cop2 ops, instructions scanned) in one function body."""
    if fn not in ext:
        return 0, 0
    lo, hi = ext[fn]
    n = ins = 0
    for a in range(lo, hi, 4):
        op = exe.word(a) >> 26
        ins += 1
        if op in (0x12, 0x32, 0x3A):  # COP2, LWC2, SWC2
            n += 1
    return n, ins


def closure(g, src):
    seen, q = {src}, deque([src])
    while q:
        cur = q.popleft()
        for t in g.get(cur, ()):
            if t not in seen:
                seen.add(t)
                q.append(t)
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(ROOT, "scratch/bin/spyro/SCUS_942.28"))
    ap.add_argument("--targets", action="store_true",
                    help="also print which known submitters each layer reaches")
    a = ap.parse_args()

    if not os.path.isfile(a.exe):
        print(f"REFUSED: no executable at {a.exe} — nothing was scanned, so this is not a result.")
        return 2

    cg = _load_callgraph()
    exe = cg.psexe.load(a.exe)
    g = cg.edges(exe)
    ext = function_extents(exe)
    print(f"exe {a.exe}: text [0x{exe.load:08X},0x{exe.text_end:08X}), "
          f"{len(ext)} function extents, {len(g)} nodes with direct edges, "
          f"{sum(len(v) for v in g.values())} edges")
    print("COP2 = GTE traffic (COP2/LWC2/SWC2). jalr edges are INVISIBLE here — a 0 with a large "
          "own body means look again, not 'no 3D'.\n")

    refusals = 0
    for addr, hint in LAYERS:
        src = addr if addr in g else cg.entry_of(exe, addr)
        seen = closure(g, src)
        tot = ins_tot = 0
        per = []
        for fn in seen:
            n, ins = cop2_of(exe, ext, fn)
            tot += n
            ins_tot += ins
            if n:
                per.append((n, fn))
        per.sort(reverse=True)
        own_n, own_ins = cop2_of(exe, ext, src)
        if ins_tot == 0:
            refusals += 1
            print(f"0x{addr:08X} {hint:22s} REFUSED: closure covered 0 instructions — the extent "
                  f"table does not contain 0x{src:08X}, so nothing was measured.")
            continue
        top = ", ".join(f"0x{f:08X}:{n}" for n, f in per[:4]) or "(none)"
        cls = "3D" if tot else "2D/logic"
        print(f"0x{addr:08X} {hint:22s} {cls:8s} closure={len(seen):3d} fns  "
              f"scanned={ins_tot:6d} insns  cop2={tot:5d}  own_body={own_n}/{own_ins}  top: {top}")
        if a.targets:
            for t in sorted(TARGETS):
                print(f"      {'REACH  ' if t in seen else 'no-path'} 0x{t:08X}  {TARGETS[t]}")

    print(f"\n{len(LAYERS)} layer(s) reported, {refusals} refused.")
    return 2 if refusals else 0


if __name__ == "__main__":
    sys.exit(main())
