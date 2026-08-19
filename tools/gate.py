#!/usr/bin/env python3
"""gate.py — the AGENT's headless gate for this port. Never `./run.sh`.

WHY THIS IS THE GATE, AND WHAT IT IS NOT. The port's first gate was a 334-line shell script that
grepped the run log for a dozen diagnostic counters — `[gpu] frame N: N prims`, `loader:`,
`moved N bytes`, `[ovload]`, `[ndiff]` matches, distinct-PPM occupancies. Those are the FRAMEWORK'S
OWN INSTRUMENTATION, not the game: a gate over them checks the checker. This gate instead drives the
ALREADY-BUILT binary headless and asserts on the OBSERVABLE GAME STATE — the same surface the
reference gates in the other ports key on (spider1/tools/gate.py, Tomba2Engine/tools/gate.py): did
the port boot the real executable, did it choose the NATIVE render path (the whole point of this
port), did it reach the TITLE SCENE, and did its NATIVE PRODUCERS actually DRAW prims?

WHAT A PASS MEANS, STATED SO IT CANNOT BE OVERREAD. A green gate means: the binary launched headless,
loaded the boot executable, the render seam chose the native PC path (or the reference path, when
that leg is asked for), the run advanced to a REAL scene (stage 13, the title, is the first one a
native-leg run reaches), the native producers REPORTED prims drawn (the picture is not empty), and no
failure pattern appeared. It says NOTHING about pixels or about the correctness of the drawn picture.
Every run prints its own denominator — lines scanned, scenes reached, prims drawn, patterns searched
— because a gate that prints only "OK" is indistinguishable from one that never ran the game.

REFUSALS (exit 2, never 0): a missing binary, a missing boot executable, an unresolvable disc, zero
output lines, a run that never reached any scene, and a binary REPLACED mid-run by a parallel rebuild
(issue 0026). Each says what it did NOT do rather than returning a clean empty pass.

USAGE
    python3 tools/gate.py boot [--seconds 40]     # THE gate: capped headless run + assertions
    python3 tools/gate.py check-log <path>        # re-run the SAME analyser over a captured log
    python3 tools/gate.py --selftest              # prove the gate FIRES (judges GOOD + mutations)
"""
from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(REPO, 'scratch', 'bin', 'spyro_port')
EXE = os.path.join(REPO, 'scratch', 'bin', 'spyro', 'SCUS_942.28')
LOGDIR = os.path.join(REPO, 'scratch', 'logs')

# ------------------------------------------------------------------------------------------------
# the log lines this gate keys on — the PORT'S OWN REPORTED STATE, not internal counters. Each is
# quoted from a real measured run; the port's own line is the proof-of-fire for what it names.
# ------------------------------------------------------------------------------------------------
RE_BOOT_LOADED = re.compile(r'\[boot\] loaded .*: entry 0x([0-9A-Fa-f]+)')
RE_RENDER_PATH = re.compile(r'\[render\] render path = (native|gte|psx_render)\b')
RE_NATIVE_LEG = re.compile(r'\[render\] render path = native\b')
RE_SCENE = re.compile(r'\[scene\] stage=(\d+) leg=(\S+)')
RE_PRODUCER_ROW = re.compile(r'^\[producers\]\s+guest\s+0x[0-9A-Fa-f]+\s+native (\d+)\s', re.MULTILINE)
RE_PRODUCER_TOTALS = re.compile(r'prims seen (\d+) = attributed (\d+)')
RE_FATAL = re.compile(r'\[FATAL:error\]|terminate called|Segmentation fault|std::bad_alloc')

# A native-leg run ABORTS on a stage that has no producer — that is the port's OWN documented
# fail-fast (render_frame.cpp). Reaching stage 13 (the title) is the first real scene; reaching it
# without aborting means the one producer fired. This is the observable floor.
TITLE_STAGE = 13

# Failure patterns over the whole log. CALIBRATED rather than guessed: a bare /\babort\b/ matches the
# port's own prose about aborting by design in several healthy startup lines, so each pattern is
# anchored to how a failure actually PRINTS.
FAIL_PATTERNS = [
    r'\[FATAL:error\]',
    r'recomp[- ]MISS',
    r'rec_dispatch miss',
    r'\[watchdog\] STUCK',
    r'Segmentation fault',
    r'std::bad_alloc',
    r'terminate called',
]

# The ordinary, EXPECTED end of a healthy capped run: the gate's SIGKILL (rc -9). Anything else
# non-zero means the port died on its own. (The old shell gate read rc=137, timeout -s KILL's status.)
KILLED_BY_GATE = -signal.SIGKILL


def refuse(msg: str) -> int:
    print(f"GATE REFUSED: {msg}", file=sys.stderr)
    return 2


def resolve_disc() -> str | None:
    """Same order run.sh uses: env, then .env, then a *.chd drop-in. The CD pump needs real media —
    the port boots with NO MEDIA otherwise, and that is a different run than the baseline."""
    d = os.environ.get('PSXPORT_SPYRO_DISC', '')
    if not d:
        envf = os.path.join(REPO, '.env')
        if os.path.isfile(envf):
            for line in open(envf):
                m = re.match(r'\s*PSXPORT_SPYRO_DISC\s*=\s*(.+?)\s*$', line)
                if m:
                    d = m.group(1)
                    break
    if not d:
        chds = [f for f in sorted(os.listdir(REPO)) if f.endswith('.chd')]
        if chds:
            d = os.path.join(REPO, chds[0])
    return d if d and os.path.isfile(d) else None


def binary_identity() -> dict:
    """md5 + mtime of the binary about to run. md5 because mtime alone cannot tell a rebuild of the
    same sources from a rebuild of different ones, and issue 0026 made mid-run replacement a real
    measured hazard (a parallel rebuild swapped scratch/bin/spyro_port while a gate was measuring it)."""
    import hashlib
    h = hashlib.md5()
    try:
        with open(BIN, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        return {'md5': h.hexdigest(),
                'mtime': time.strftime('%Y-%m-%dT%H:%M:%S', time.localtime(os.path.getmtime(BIN)))}
    except OSError as e:
        return {'md5': f'UNREADABLE({e.__class__.__name__})', 'mtime': ''}


# ------------------------------------------------------------------------------------------------
# the analyser — a PURE function of (log text, exit code, seconds), so --selftest and check-log feed
# it captured and mutated logs. A gate whose judgement can only be exercised by launching the game is
# a gate nobody can prove fires.
# ------------------------------------------------------------------------------------------------

class Report:
    """Collects PASS/FAIL/NOTE/WARN and prints a machine-computable tally. PASS/FAIL are checks;
    NOTE/WARN are reported backlog and are NOT checks — the tally line is the only correct count."""

    def __init__(self, label: str) -> None:
        self.label = label
        self.npass = self.nfail = self.nnote = self.nwarn = 0
        self.fail = 0

    def ok(self, name: str, detail: str) -> None:
        self.npass += 1
        print(f"  [gate:{self.label}] PASS  {name:<44} {detail}")

    def bad(self, name: str, detail: str) -> None:
        self.nfail += 1
        self.fail = 1
        print(f"  [gate:{self.label}] FAIL  {name:<44} {detail}")

    def note(self, name: str, detail: str) -> None:
        self.nnote += 1
        print(f"  [gate:{self.label}] NOTE  {name:<44} {detail}")

    def warn(self, name: str, detail: str) -> None:
        self.nwarn += 1
        print(f"  [gate:{self.label}] WARN  {name:<44} {detail}")

    def tally(self) -> int:
        print(f"[gate:{self.label}] tally: {self.npass} PASS, {self.nfail} FAIL "
              f"(checks = {self.npass + self.nfail}); {self.nnote} NOTE, {self.nwarn} WARN (not checks)")
        return self.fail


def analyse(out: str, rc: int | None, secs: float, label: str, log_hint: str = '',
            rep: 'Report | None' = None, expect_native: bool = True) -> int:
    """Pure judgement of one run. Returns 0 pass / 1 fail / 2 refuse; prints its own denominator.

    `rep` is the report to tally into; when None (check-log, selftest) a fresh one is created. The
    caller passes its OWN report so the analyser's PASS/FAIL rows land in the same tally as the
    static checks — otherwise a FAIL the analyser printed would vanish from the gate's final count.

    `expect_native`: whether this run was asked to use the NATIVE leg. Default (the gate) is True —
    a run that came back on the reference leg when native was asked for is a regression. Pass False
    for --psx-render, where the reference leg is the explicit request and 'native not exercised' is
    the intended state, reported as a note.
    """
    if rep is None:
        rep = Report(label)
    lines = out.count('\n')
    scenes = RE_SCENE.findall(out)
    producer_rows = [int(m) for m in RE_PRODUCER_ROW.findall(out)]
    boot = RE_BOOT_LOADED.search(out)
    render_path = RE_RENDER_PATH.search(out)
    native = bool(RE_NATIVE_LEG.search(out))
    fatal = RE_FATAL.search(out)

    print(f"[gate:{label}] exit={rc} in {secs:.1f}s · {lines} output line(s) · "
          f"scenes reached: {', '.join(sorted({s for s, _l in scenes})) or 'NONE'} · "
          f"native producer prims: {sum(producer_rows) or 0}")
    if log_hint:
        print(f"[gate:{label}] {log_hint}")

    if lines == 0:
        return refuse(f"ZERO output lines in {secs:.1f}s (exit {rc}). Nothing was observed, so "
                      f"nothing is proven.")

    # THE OBSERVABLE GAME STATE. Each of these is what the GAME did, not what the framework's counters
    # said. A healthy run is either killed by the gate (the capped-run ending) or ends cleanly on its
    # own (rc=0, e.g. PSXPORT_NATIVE_FRAMES). Anything else non-zero is the port dying — a segfault,
    # an abort, a fail-fast the pattern list should name.
    if rc in (KILLED_BY_GATE, 0):
        rep.ok("port ended cleanly",
               "killed by gate (rc=-9)" if rc == KILLED_BY_GATE else "clean exit (rc=0)")
    else:
        rep.bad("port ended cleanly", f"port exited on its own (rc={rc})")

    if boot:
        rep.ok("boot executable loaded", f"entry 0x{boot.group(1)}")
    else:
        rep.bad("boot executable loaded", "no '[boot] loaded … entry 0x…' line — the game never started")

    if render_path:
        rep.ok("render path chosen", f"{render_path.group(1)}"
               + (" (native = the shipping PC path)" if native else " (reference leg)"))
        if not native and expect_native:
            rep.bad("native render path", "this run came back on the reference leg though the native "
                    "leg was asked for — 'the port renders natively' is not proven")
        elif not native:
            rep.note("native render path", "reference leg (asked for via --psx-render) — the native "
                     "PC renderer was NOT exercised, which is the intended state")
    else:
        rep.bad("render path chosen", "no '[render] render path = …' line")

    # Did the run reach a REAL scene? Reaching the title (stage 13) without aborting is the first
    # observable milestone of the native leg — the one producer fires there.
    stage_set = sorted({int(s) for s, _l in scenes})
    if stage_set:
        rep.ok("run reached real scene(s)", f"stages {', '.join(map(str, stage_set))}"
               + ("" if TITLE_STAGE in stage_set else f" (title stage {TITLE_STAGE} NOT reached)"))
        if TITLE_STAGE not in stage_set:
            rep.bad("title scene reached", f"stage {TITLE_STAGE} never appeared (reached "
                    f"{stage_set}) — the run did not get to the first natively-produced scene")
    else:
        rep.bad("run reached real scene(s)", "no '[scene] stage=N' line ever appeared — cannot say "
                "the game reached any scene")

    # Did the NATIVE producers draw? The producer DB is the port's own report of what its PC-native
    # producers emitted. On the NATIVE leg a zero here means the picture is empty — the port aborts
    # on a stage with no producer, so a run that reached a scene and reported 0 prims did not produce
    # its picture. On the REFERENCE leg (--psx-render) there are no native producers by construction —
    # the guest draws the picture — so this is reported, not demanded.
    total_prims = sum(producer_rows)
    if total_prims > 0:
        rep.ok("native producers drew prims", f"{total_prims} prim(s) attributed to native producers")
    elif native:
        rep.bad("native producers drew prims", "0 prims attributed — no native producer emitted, so "
                "the picture was empty")
    else:
        rep.note("native producers drew prims", "0 — reference leg, so no native producer is expected "
                 "to have emitted")

    # Failure patterns over the whole log — a recomp miss or a watchdog STUCK is a fail even if the
    # observable states above happened to look right.
    hits = []
    for pat in FAIL_PATTERNS:
        mm = re.search(pat, out)
        if mm:
            ctx = out[max(0, mm.start() - 100):mm.end() + 200].replace('\n', ' | ')
            hits.append((pat, ctx))
    if hits:
        print(f"[gate:{label}] FAIL — {len(hits)} failure pattern(s) matched:")
        for pat, ctx in hits:
            print(f"    /{pat}/  …{ctx}…")
        rep.fail = 1
        rep.nfail += 1

    return 1 if rep.fail else 0


# ------------------------------------------------------------------------------------------------
# the static checks — the non-run pieces of the gate: shipped producer keys vs the guest image,
# ledger/codemap self-consistency, and the framework pin check. Each is a subprocess over an
# already-present tool; each prints its own denominator.
# ------------------------------------------------------------------------------------------------

def _run(cmd: list, env: dict | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=REPO)


# Owned bodies that tools/transcribe.py emits from the recompiled substrate. Each is re-derived
# back to its generated source by the gate; add a row here when a new body is transcribed.
TRANSCRIBED_BODIES = [
    ('0x800258F0', 'game/core/world_body.inc'),   # RenderWorldChunks — the world/ground renderer
]


def run_static_checks(rep: Report, disc: str) -> None:
    # 1. THE SHIPPED PRODUCER KEYS vs THE GUEST IMAGE THEY WERE MEASURED FROM. A ProducerScope key is
    # a MEASURED CONSTANT that decides which DB row a native draw is charged to; until it was gated,
    # a transposed digit shipped a plausible wrong row with every other check green. verify_producers
    # parses the shipping .cpp and re-derives the address from the overlay bytes, and --selftest
    # mutates each side and requires the mutant to be caught. This is shipped-vs-measured, not meta:
    # it checks the port against the binary it recompiles.
    p = _run([sys.executable, 'tools/verify_producers.py', '--selftest'])
    if p.returncode == 0:
        rep.ok("producer keys == measured (+selftest)",
               f"{p.stdout.count('  ok ')} case(s) incl. {p.stdout.count('mutant:')} mutant(s) caught")
    else:
        rep.bad("producer keys == measured (+selftest)", "see output")
        for line in p.stdout.splitlines():
            if 'FAIL' in line or 'REFUSED' in line:
                print(f"        {line.strip()}")

    # 1b. THE GENERATED-TRANSCRIPTION BODIES vs THE SUBSTRATE THEY WERE RENDERED FROM. An owned
    # body produced by tools/transcribe.py is byte-exact BY CONSTRUCTION, but only for the substrate
    # it was emitted from: regenerate generated/ and the committed .inc can silently describe an
    # older recompilation. `transcribe.py check` re-derives the generated source from the committed
    # body and requires an exact match, so that drift fails here instead of surfacing as a
    # differential divergence nobody can attribute. --selftest first, so a check that cannot fail
    # is never mistaken for a check that passed: it feeds the round-trip eight corruptions (dropped
    # statement, substituted register, conditionalised delay slot, altered offset/mask/constant/GTE
    # opcode, reordered statements) and requires every one to be caught.
    p = _run([sys.executable, 'tools/transcribe.py', '--selftest'])
    if p.returncode == 0:
        rep.ok("transcribe round-trip selftest",
               f"{p.stdout.count('[ok]')} case(s), corruptions all caught")
    else:
        rep.bad("transcribe round-trip selftest", "see output")
        for line in p.stdout.splitlines():
            if 'BAD' in line:
                print(f"        {line.strip()}")

    for addr, body in TRANSCRIBED_BODIES:
        p = _run([sys.executable, 'tools/transcribe.py', 'check', addr, '--body', body])
        label = f"{body} == generated {addr}"
        if p.returncode == 0:
            rep.ok(label, p.stdout.strip().splitlines()[-1].split('  ')[-1])
        else:
            rep.bad(label, "the committed body no longer inverts to the generated source")
            for line in p.stdout.splitlines():
                print(f"        {line.strip()}")

    # 2. AND THE SHIPPING PATH MUST ACTUALLY FIRE — a short CAPPED native-leg run (~2s) whose DB must
    # carry a row keyed at the shipped constant with prims > 0. The static check is green with the
    # ProducerScope line deleted; this is the half that catches that. --db-expect names only the
    # producers THIS run can reach: the native leg reaches the title (stage 13), so its producers
    # (titlefx, terrain) fire; the field producers (spriteq, pairedactor) cannot and are checked
    # statically instead. A deleted scope is still caught — its label is in the expected set and the
    # DB would lack its row.
    env = dict(os.environ)
    env.update({
        'PSXPORT_NATIVE_FRAMES': '3000',
        'PSXPORT_NOPACE': '1',
        'PSXPORT_NOAUDIO': '1',
        'PSXPORT_ASSET_DIR': os.path.join(REPO, 'external', 'psxport'),
        'PSXPORT_SPYRO_DISC': disc,
    })
    log = os.path.join(LOGDIR, f'gate-producers-{time.strftime("%Y%m%d-%H%M%S")}.log')
    try:
        p = subprocess.run([BIN, EXE], capture_output=True, text=True, env=env, cwd=REPO,
                           timeout=120)
        out = (p.stdout or '') + (p.stderr or '')
        rc = p.returncode
    except subprocess.TimeoutExpired:
        out = ''
        rc = -9
    open(log, 'w').write(out)
    m = re.search(r'wrote .* -> (scratch/producers/run-[^ ]+\.jsonl)', out)
    if rc != 0 or not m:
        rep.bad("producer scope fired in a run",
                f"capped native-leg run rc={rc}, wrote {m.group(1) if m else 'NO JSONL'} — see {log}")
    else:
        p2 = _run([sys.executable, 'tools/verify_producers.py', '--db', m.group(1),
                   '--db-expect', 'titlefx:spriteEmit,terrain:F3G3'])
        if p2.returncode == 0:
            rep.ok("producer scope fired in a run",
                   re.sub(r'\s+', ' ', p2.stdout.replace('  RAN ', ''))[:70])
        else:
            rep.bad("producer scope fired in a run", f"see {log}")

    # 3. Ledger + codemap self-consistency. Not run-time properties, but these run every iteration
    # and a contradictory ledger is silently served as fact by every later `info.py brief`.
    p = _run([sys.executable, 'tools/info.py', 'check', '--no-stale'])
    if p.returncode == 0:
        rep.ok("info ledger self-consistent", "ok")
    else:
        rep.bad("info ledger self-consistent",
                f"{sum(1 for l in p.stdout.splitlines() if re.search(r'INCONSISTENT|NO FALSIFIER|DISTRUSTED INSTRUMENT', l))} problem(s)")
        for line in p.stdout.splitlines():
            if re.search(r'INCONSISTENT|NO FALSIFIER|DISTRUSTED INSTRUMENT', line):
                print(f"        {line.strip()}")

    p = _run([sys.executable, 'tools/codemap.py', 'check'])
    if p.returncode == 0:
        rep.ok("codemap has no drift", next((l for l in p.stdout.splitlines() if 'scanned' in l), 'ok'))
    else:
        n = sum(1 for l in p.stdout.splitlines() if re.match(r'^  (⬜|❌)', l))
        rep.bad("codemap has no drift", f"{n} drifted reference(s) — docs/codemap.md")
        for line in p.stdout.splitlines():
            if re.match(r'^  (⬜|❌)|^  scanned ', line):
                print(f"        {line.strip()}")

    # 4. The FRAMEWORK PIN: what this tree was BUILT against (recorded by CMake into
    # build/psxport_resolved.txt) must be what psxport.pin RECORDS, or a fresh clone builds a
    # different framework than was tested — how this tree once shipped a pin whose GameHooks lacked a
    # field the game used. Not fatal when the tree was never configured (the tool says so itself).
    p = _run([sys.executable, 'tools/psxport_sync.py', '--check'])
    if p.returncode == 0:
        rep.ok("psxport pin matches built framework",
               next((l for l in p.stdout.splitlines() if 'OK' in l), 'ok'))
    elif p.returncode == 2:
        rep.note("psxport pin matches built framework",
                 next((l for l in p.stdout.splitlines() if 'REFUSED' in l), 'nothing asserted'))
    else:
        rep.bad("psxport pin matches built framework",
                next((l for l in p.stdout.splitlines() if 'FAILED' in l), 'built != recorded pin'))

    # 5. Claim staleness: reported every run and never silent (a stale claim reads as a current
    # description of the renderer and can buy a whole native producer as a workaround — C099). NOT a
    # check: it is a re-verification backlog, and failing the gate on it would teach everyone to
    # ignore the line.
    p = _run([sys.executable, 'tools/info.py', 'claim', 'check'])
    txt = p.stdout
    num = lambda key: next((int(mm.group(1)) for mm in [re.search(rf'^ *{re.escape(key)}[ .]*(\d+)', line, re.M) for line in txt.splitlines()] if mm), 0)
    stale, checked, unseen = num('stale (code moved)'), num('CHECKED'), num('*** CANNOT SEE')
    rep.note("claim staleness (not fatal)", f"{stale} stale / {checked} checked, {unseen} UNCHECKED")

    # 6. Open 'blocker' entries — reported only when the gate passed, as WARN. A blocker asserts the
    # port cannot get past it; a passing gate says otherwise, so each one is a contradiction.
    if not rep.fail:
        p = _run([sys.executable, 'tools/catalog.py', 'stale', '--count'])
        try:
            n = int(p.stdout.strip())
        except ValueError:
            n = 0
        if n > 0:
            rep.warn("open 'blocker' issues", f"{n} — gate passes, so each no longer blocks")
        else:
            rep.ok("open 'blocker' issues", "none")


# ------------------------------------------------------------------------------------------------
# the launcher
# ------------------------------------------------------------------------------------------------
def cmd_boot(args) -> int:
    if not os.path.isfile(BIN):
        return refuse(f"{BIN} does not exist — NOTHING WAS RUN. Build first: cmake --build build "
                      f"--target spyro_port -j$(nproc). Do not read this as a pass.")
    if not os.path.isfile(EXE):
        return refuse(f"{EXE} does not exist — NOTHING WAS RUN. The boot executable is extracted "
                      f"from the disc by tools/ensure_recomp.py (run.sh's job, and run.sh is the "
                      f"user's). Provision it once, then re-run this gate.")
    disc = resolve_disc()
    if not disc:
        return refuse("no disc image resolved (PSXPORT_SPYRO_DISC, .env, or a *.chd here) — "
                      "NOTHING WAS RUN. Without media the [cd] pump serves nothing.")

    env = dict(os.environ)
    env['PSXPORT_SPYRO_DISC'] = disc
    env['PSXPORT_NOAUDIO'] = '1'
    env['PSXPORT_NOPACE'] = '1'          # the ONLY switch that means "frames, not real time"
    env['PSXPORT_WATCHDOG'] = str(args.watchdog)
    # The PORT owns its frame loop unconditionally now (frame_loop.cpp) — reaching a drawn frame
    # requires NO env flag, which is the point. The render seam picks native (default) or the
    # reference (--psx-render) from the framework's RenderMode.
    env['PSXPORT_DEBUG'] = 'scene,producers'   # the two OBSERVABLE channels the analyser keys on
    env['PSXPORT_ASSET_DIR'] = os.path.join(REPO, 'external', 'psxport')
    # PSXPORT_NATIVE_FRAMES ends the run CLEANLY after N presented fields, so it exits 0 and emits
    # the producer DB — the run's own report of what its native producers drew. An UNCAPPED native
    # run would be killed by the gate (no DB) and would also reach the FIELD (stage 0), where the
    # native leg aborts by design (no producer yet — render.h). Capping at the title keeps the run
    # on the first natively-produced scene, which is the observable milestone, and makes the exit
    # clean rather than an abort. Boot consumes between 1600 and 3000 presented fields (C169), so
    # 3000 is the floor for a run that must reach the title and emit the DB.
    env['PSXPORT_NATIVE_FRAMES'] = str(args.frames)
    if args.psx_render:
        env['PSXPORT_RENDER_PSX'] = '1'        # the reference leg: guest driver 0x8001ED5C

    os.makedirs(LOGDIR, exist_ok=True)
    logpath = os.path.join(LOGDIR, f'gate-boot-{time.strftime("%Y%m%d-%H%M%S")}.log')

    # PROCESS GROUP, not a bare child: subprocess kills only the DIRECT child, and killing the wrapper
    # leaves the port alive and reparented to pid 1 — an orphan holding the GPU is exactly what the
    # next run then contends with. The launch gets its own session; the hang path group-kills.
    before = binary_identity()
    print(f"[gate:boot] launching {BIN} capped at {args.seconds}s / {args.frames} presented fields, "
          f"PSXPORT_WATCHDOG={args.watchdog}s frame-progress")
    t0 = time.time()
    p = subprocess.Popen([BIN, EXE], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         env=env, cwd=REPO, stdin=subprocess.DEVNULL, start_new_session=True)
    try:
        so, se = p.communicate(timeout=args.seconds)
        rc = p.returncode
    except subprocess.TimeoutExpired:
        try:
            pgid = os.getpgid(p.pid)
            os.killpg(pgid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        so, se = p.communicate(timeout=30)
        rc = KILLED_BY_GATE            # the gate ended it: the wall-clock cap won before --frames
    secs = time.time() - t0
    out = (so or '') + (se or '')
    with open(logpath, 'w') as f:
        f.write(out)

    after = binary_identity()
    if after['md5'] != before['md5']:
        return refuse(f"the binary was REPLACED mid-run ({before['md5'][:12]} -> {after['md5'][:12]}). "
                      f"A parallel rebuild replaced scratch/bin/spyro_port while this gate was "
                      f"measuring it, so these numbers describe a mixture of two builds. Refusing to "
                      f"report — re-run once the other build has settled. (issue 0026)")

    rep = Report('boot')
    rc_gate = analyse(out, rc, secs, 'boot', f'log: {logpath}', rep, expect_native=not args.psx_render)
    if rc_gate == 2:
        return 2
    run_static_checks(rep, disc)
    fail = rep.tally()
    if fail:
        print(f"[gate] FAIL — see {logpath}")
        return 1
    print("[gate] PASS")
    return 0


def cmd_check_log(args) -> int:
    if not os.path.isfile(args.path):
        return refuse(f"{args.path} does not exist — NOTHING WAS ANALYSED.")
    out = open(args.path, errors='replace').read()
    return analyse(out, args.rc, args.seconds, 'check-log', f'analysed file: {args.path}',
                   expect_native=args.expect_native)


# ------------------------------------------------------------------------------------------------
# --selftest — PROVE THE GATE FIRES. A gate nobody has seen fail is not a gate. The GOOD case is a
# realistic capture of the OBSERVABLE state a real native-leg run produces; each BAD case is that
# same text with one thing broken, so a failure is attributable to exactly that mutation.
# ------------------------------------------------------------------------------------------------
GOOD_LOG = """\
[watchdog] armed: 15s frame-progress timeout (45s grace for the first frame)
[boot] loaded scratch/bin/spyro/SCUS_942.28: entry 0x8005B8E0 load 0x80010000 text 0x65800 sp 0x801FFFF0
[render] render path = native — geometry from PC-NATIVE producers, rasterized by the PC rasterizer (SDL_GPU), PC enhancements ALLOWED
[render] native path: ONE producer (stage 13 front-end sprites, C167). Aborts on a stage with no producer; guest-drawn parts WITHIN a producer's stage only warn. PSXPORT_RENDER_PATH=gte for the reference picture (guest driver 0x8001ED5C).
[scene] stage=13 leg=native arm=SPLIT on [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4
[scene] stage=13 leg=native arm=SPLIT on [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4
[producers] run-end: 1 row(s); prims seen 1380 = attributed 1380 + unscoped-native 0 + guest-origin 0 + gp0-anon 0 + span-miss 0 + span-no-fn 0
[producers]   guest   0x8007CD38  native 1380  guest 0  frames 697 (f585..f1282)  titlefx:spriteEmit
"""


def selftest() -> int:
    cases: list[tuple[str, str, int, int, float]] = []   # name, text, rc, expected_exit, secs

    cases.append(("GOOD baseline capture (must PASS)", GOOD_LOG, -9, 0, 40))

    # 1. the boot executable never loaded
    cases.append(("boot exe never loaded",
                  GOOD_LOG.replace("[boot] loaded scratch", "[boot] xxxxxx scratch"), -9, 1, 40))
    # 2. the port chose the REFERENCE path, not native — 'native renders' is not proven
    cases.append(("reference leg chosen",
                  GOOD_LOG.replace("render path = native", "render path = gte"), -9, 1, 40))
    # 3. never reached a real scene (no [scene] line)
    noscene = re.sub(r"\[scene\][^\n]*\n", "", GOOD_LOG)
    cases.append(("no scene reached", noscene, -9, 1, 40))
    # 4. reached a stage but NOT the title (stage 13)
    cases.append(("title stage not reached",
                  GOOD_LOG.replace("stage=13 leg=native", "stage=0 leg=native"), -9, 1, 40))
    # 5. native producers drew NOTHING — empty picture
    cases.append(("native producers drew nothing",
                  GOOD_LOG.replace("native 1380  guest 0", "native 0  guest 0")
                           .replace("prims seen 1380 = attributed 1380", "prims seen 0 = attributed 0"),
                  -9, 1, 40))
    # 6. a recomp miss
    cases.append(("recomp MISS", GOOD_LOG + "[hle:warn] [recomp-MISS 0] no recompiled fn for 0x8001E91C\n", -9, 1, 40))
    # 7. the frame-progress watchdog fired
    cases.append(("frame watchdog STUCK",
                  GOOD_LOG + "\n[watchdog] STUCK: no frame presented within the timeout — backtrace:\n", -9, 1, 40))
    # 8. the port died on its own (a healthy run is killed by the gate)
    cases.append(("port exited on its own", GOOD_LOG, -signal.SIGABRT, 1, 40))
    # 9. zero output must REFUSE
    cases.append(("zero output => REFUSE", "", -9, 2, 40))

    print("=" * 96)
    print("SELFTEST — every case below is judged by the SAME analyse() the real gate uses.")
    print("Expected exits: 0 = pass, 1 = fail, 2 = refuse (nothing proven).")
    print("=" * 96)
    bad = 0
    for i, (name, text, rc, want, secs) in enumerate(cases, 1):
        print(f"\n----- case {i}/{len(cases)}: {name}  (expect exit {want}) -----")
        got = analyse(text, rc, secs, f'self{i}')
        verdict = 'OK' if got == want else 'SELFTEST FAILURE'
        if got != want:
            bad = 1
        print(f"----- case {i}: exit {got}, expected {want} -> {verdict}")

    # The missing-binary refusal, exercised through the REAL launcher path so the refusal that guards
    # every real invocation is itself covered.
    print(f"\n----- case {len(cases) + 1}: missing binary via the real launcher (expect exit 2) -----")
    global BIN
    saved, BIN = BIN, os.path.join(REPO, 'scratch', 'bin', 'does_not_exist_selftest')
    got = cmd_boot(argparse.Namespace(seconds=5, frames=3000, watchdog=5, debug='', psx_render=False))
    BIN = saved
    print(f"----- case {len(cases) + 1}: exit {got}, expected 2 -> {'OK' if got == 2 else 'SELFTEST FAILURE'}")
    if got != 2:
        bad = 1

    n = len(cases) + 1
    n_pass = sum(1 for _n, _t, _r, w, _s in cases if w == 0)
    n_fail = sum(1 for _n, _t, _r, w, _s in cases if w == 1)
    n_refuse = sum(1 for _n, _t, _r, w, _s in cases if w == 2) + 1   # +1: the launcher refusal
    print("\n" + "=" * 96)
    if bad:
        print(f"SELFTEST FAILED over {n} case(s): the gate does not judge as documented. Do not trust "
              f"any verdict it produced.")
        return 1
    print(f"SELFTEST PASSED over {n} case(s): {n_pass} known-good capture(s) passed, and "
          f"{n - n_pass} broken variants were caught — {n_fail} FAIL, {n_refuse} REFUSE — each keyed "
          f"on exactly one mutated line. THE GATE HAS BEEN SEEN TO FAIL.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--selftest', action='store_true',
                    help='prove the gate fires: judge a known-good capture plus one mutation per '
                         'assertion, and a real missing-binary refusal')
    sub = ap.add_subparsers(dest='cmd')
    b = sub.add_parser('boot', help='capped headless launch + assertions (THE gate)')
    b.add_argument('--seconds', type=int, default=40,
                   help='wall-clock cap for the run (default 40). The run is also capped by '
                        '--frames; whichever ends it first. A clean exit at the frame cap is the '
                        'healthy end here — the port emits its producer DB and exits 0.')
    b.add_argument('--frames', type=int, default=3000,
                   help='PSXPORT_NATIVE_FRAMES: end the run cleanly after this many presented '
                        'fields, emitting the producer DB (default 3000 — the floor for boot to '
                        'reach the title, C169). A native-leg run reaches the FIELD and aborts '
                        'beyond the title, so capping at the title is what makes the run clean.')
    b.add_argument('--watchdog', type=int, default=15,
                   help="PSXPORT_WATCHDOG: the port's own frame-progress timeout in seconds "
                        "(default 15). A stall aborts in-band with a guest backtrace.")
    b.add_argument('--debug', default='',
                   help='extra PSXPORT_DEBUG channels to ADD to scene,producers')
    b.add_argument('--psx-render', action='store_true',
                   help='run the REFERENCE leg (PSXPORT_RENDER_PATH=gte) instead of the native PC '
                        'path. The analyser then notes that native rendering was not exercised.')
    b.set_defaults(fn=cmd_boot)
    c = sub.add_parser('check-log', help='run the SAME analyser over a captured log')
    c.add_argument('path')
    c.add_argument('--seconds', type=float, default=40, help="the captured run's duration")
    c.add_argument('--rc', type=int, default=-9, help="the captured run's exit code (default -9 = killed by gate)")
    c.add_argument('--expect-native', action='store_true', default=True,
                   help="require the run to have used the native leg (default). Pass --no-expect-native "
                        "for a reference-leg (--psx-render) capture.")
    c.set_defaults(fn=cmd_check_log)
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.cmd:
        ap.print_help()
        return 2
    return args.fn(args)


if __name__ == '__main__':
    sys.exit(main())
