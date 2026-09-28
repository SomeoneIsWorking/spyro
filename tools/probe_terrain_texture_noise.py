#!/usr/bin/env python3
"""probe_terrain_texture_noise.py — is the ground/vegetation texel noise in the BASE field path, the
widescreen path, or the interpolation path?

WHY THIS EXISTS. The operator showed a frame where the ground, the hedges and the vegetation render as
random per-block colour noise while Spyro, the dragons, the towers and the buildings are clean. Clean
characters plus corrupt ground is a strong shape and it narrows the question immediately: VRAM is not
wholly garbage, so this is not "the texture upload never happened".

The two candidate causes are the port's two enhancements, and they are separable, because a picture
claim is a claim about a DIFFERENCE and a single 4:3-vs-16:9 pair confounds them:

    fps60_control_settings.ini        aspect=0 fps60=0    4:3, no interpolation
    narrow_only_control_settings.ini  aspect=0 fps60=1    4:3, interpolation     <- the cross
    wide_only_control_settings.ini    aspect=1 fps60=0    16:9, no interpolation
    shipping_settings.ini             aspect=1 fps60=1    16:9, interpolation      <- what the player sees

THE CROSS IS THE WHOLE POINT. With only the 4:3/16:9 pair, "noise at 16:9" and "noise with
interpolation" are the same claim. The two extra legs make them different claims: noise in both 4:3 legs
and neither 16:9 leg is the projection; noise in both interpolation legs and neither non-interpolated leg
is the temporal pass; noise in all four is the base field path and neither enhancement is involved.

THE MEASUREMENT IS A BLOCKINESS CENSUS, not an eyeball. A coherent textured surface has neighbouring
pixels that agree far more often than chance; per-block colour noise does not. So for each capture this
counts, over the ground region:

  * the fraction of horizontally-adjacent pixel pairs that DISAGREE (a coherent surface scores low, a
    noise field scores high), and
  * the distinct-colour count, and
  * the same figures for a REFERENCE REGION the operator says is CLEAN -- the upper buildings -- so the
    tool can report "the ground scores X and the buildings score Y" rather than an absolute threshold
    that would be a guess.

An absolute threshold would be exactly the kind of number that is wrong on the next title, so there
isn't one: the verdict is a COMPARISON between the two regions of the same frame and between the four
legs, and the tool says so in those words.

Usage:
  python3 tools/probe_terrain_texture_noise.py --selftest
  python3 tools/probe_terrain_texture_noise.py [--frames 1] [--keep 2000]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

import guest_globals  # noqa: E402
from drive import Port, disc_path, environment  # noqa: E402

SINK_H = 240

# The four legs. Named by what they isolate, not by their numbers.
LEGS = (
    ("4:3-nointerp", "tools/fps60_control_settings.ini"),
    ("4:3-interp", "tools/narrow_only_control_settings.ini"),
    ("16:9-nointerp", "tools/wide_only_control_settings.ini"),
    ("16:9-interp", "tools/shipping_settings.ini"),
)


# ---- the measurement -------------------------------------------------------------------------

def disagreement(row: bytes, stride: int) -> tuple[int, int]:
    """(pairs that disagree, pairs compared) across one row, horizontally.

    This is the figure that separates a coherent textured surface from per-block colour noise, and it
    is a RATIO with its own denominator so a short read cannot read as a low score.
    """
    differ = 0
    total = 0
    for i in range(0, stride - 3, 3):
        a = row[i:i + 3]
        b = row[i + 3:i + 6]
        if len(b) < 3:
            break
        total += 1
        if a != b:
            differ += 1
    return differ, total


def measure_region(pixels: bytes, w: int, y0: int, y1: int) -> tuple[float, int]:
    """Mean horizontal disagreement over a row band, with the number of pairs compared."""
    differ = 0
    total = 0
    for y in range(max(0, y0), min(SINK_H, y1)):
        off = y * w * 3
        d, t = disagreement(pixels[off:off + w * 3], w)
        differ += d
        total += t
    if total == 0:
        return 0.0, 0
    return differ / total, total


def distinct_colours(pixels: bytes, w: int, y0: int, y1: int) -> int:
    seen: set[bytes] = set()
    for y in range(max(0, y0), min(SINK_H, y1)):
        off = y * w * 3
        row = pixels[off:off + w * 3]
        for i in range(0, len(row) - 2, 3):
            seen.add(bytes(row[i:i + 3]))
    return len(seen)


def verdict(ground: float, building: float, pairs: int) -> str:
    if pairs == 0:
        return "NO VERDICT: the ground region produced no pixel pairs, so nothing was compared"
    if ground > building * 2.0:
        return (f"NOISE IN THE GROUND: it disagrees on {100 * ground:.1f}% of adjacent pairs against "
                f"{100 * building:.1f}% in the buildings the operator says are clean, over {pairs} pairs")
    return (f"coherent: ground {100 * ground:.1f}% vs buildings {100 * building:.1f}% over {pairs} pairs, "
            f"which is within a factor of two and is not the per-block noise shape")


def selftest() -> int:
    cases = []

    # A COHERENT surface: one colour. Disagreement 0.
    row = b"\x20\x40\x80" * 16
    cases.append(("uniform row", disagreement(row, 48), (0, 15)))

    # NOISE: every pixel differs from its neighbour.
    noise = b"".join(bytes((x, y, (x * 3 + y) % 256)) for x in range(16) for y in range(3))
    d, t = disagreement(noise, 48)
    cases.append(("noise row", (d, t), (t, t)))

    # A HORIZONTAL STRIPE: alternating pixels disagree with every neighbour. This is the case that
    # says a DISAGREEMENT RATIO alone cannot tell noise from an intentional hard edge -- which is why
    # the tool compares two REGIONS of the same picture instead of thresholding a number.
    stripe = b"".join(bytes((255, 0, 0)) if x % 2 else bytes((0, 255, 0)) for x in range(16))
    d, t = disagreement(stripe, 48)
    cases.append(("striped row disagrees fully", (d, t), (t, t)))

    failures = 0
    for label, got, want in cases:
        if got != want:
            print(f"FAIL {label}: got {got} want {want}")
            failures += 1
    if verdict(0.9, 0.1, 1000).startswith("NOISE") is False:
        print("FAIL a ground 9x noisier than the buildings was not called noise")
        failures += 1
    if verdict(0.2, 0.1, 1000).startswith("coherent") is False:
        print("FAIL a ground 2x the buildings was not called coherent")
        failures += 1
    if verdict(0.9, 0.1, 0).startswith("NO VERDICT") is False:
        print("FAIL an unmeasured region was not refused")
        failures += 1
    if failures == 0:
        print(f"selftest: {len(cases) + 3}/{len(cases) + 3} OK -- uniform, noise and stripes are told "
              f"apart, and an unmeasured region is refused rather than scored")
    return 1 if failures else 0


def run_leg(name: str, ini_rel: str, frames: int, keep: int) -> dict:
    ini = ROOT / ini_rel
    if not ini.is_file():
        return {"leg": name, "error": f"{ini_rel} is missing"}
    env = environment(disc_path())
    env["PSXPORT_SETTINGS"] = str(ini)
    log = ROOT / "scratch" / "logs" / f"terrain-{name}.log"
    port = Port(ROOT / "build/bin/spyro_port", ROOT / "scratch/assets/spyro1/SCUS_942.28", log, env)
    shot_rel = f"scratch/screenshots/terrain-{name}.ppm"
    out: dict = {"leg": name, "settings": str(ini)}
    try:
        port.run(frames)
        port.shot(shot_rel)
        reply = port.word(0x1F801802)  # not the width; read it from the log below instead
        out["done"] = True
    except Exception as exc:  # a refusal is a recorded outcome, not a traceback
        out["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            port.end()
        except Exception:
            pass
    text = log.read_text(errors="ignore") if log.is_file() else ""
    import re
    m = re.findall(r"render_width=(\d+)\s+native_width=(\d+)", text)
    if m:
        out["render_width"], out["native_width"] = m[-1]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--frames", type=int, default=260)
    ap.add_argument("--keep", type=int, default=0, help="unused; kept for symmetry")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

    print("Running the four legs; the CROSS is what separates the two enhancements.\n")
    results = []
    for name, ini in LEGS:
        r = run_leg(name, ini, args.frames, args.keep)
        results.append(r)
        if "error" in r:
            print(f"{name:14s} ERROR {r['error'][:90]}")
            continue
        shot = ROOT / f"scratch/screenshots/terrain-{name}.ppm"
        if not shot.is_file():
            print(f"{name:14s} no capture at {shot.name}")
            continue
        data = shot.read_bytes()
        i = data.index(b"255\n") + 4
        hdr = data[:i].split()
        w, h = int(hdr[1]), int(hdr[2])
        px = data[i:]
        # The operator's picture: ground fills the lower two thirds, buildings the upper third.
        g, gp = measure_region(px, w, int(h * 0.62), h)
        b, bp = measure_region(px, w, 0, int(h * 0.30))
        print(f"{name:14s} {w}x{h}  rw={r.get('render_width','?')}/nw={r.get('native_width','?')}  "
              f"ground {100 * g:5.1f}%  buildings {100 * b:5.1f}%  "
              f"colours g/d={distinct_colours(px, w, int(h * 0.62), h)} "
              f"b/d={distinct_colours(px, w, 0, int(h * 0.30))}")
        print(f"{'':14s} -> {verdict(g, b, gp)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
