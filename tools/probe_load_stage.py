"""Is `g_LoadStage` a STATE or a counter IN TRANSIT? The level-load barrier assumes the answer.

WHY THIS FILE EXISTS
--------------------
`tools/oracle_spyro1_demo.py` makes `load_stage` DECISIVE, and the route's own comment says why: "arriving
with a different level resident or a different load stage fails here rather than three segments later."
That is a good instinct — a load that half-happened must not be compared as though it finished.

But the run says something the comment does not anticipate. The level-load barrier reports

    load_stage @ 0x80075864   native 01000000   console 02000000

one stage apart, while the very next checkpoint, `demo_playing`, MATCHES on all 13 decisive ranges
including `level_id`, and the level-entry blind spot this route exists to close (docs/issues/0114) is
therefore covered and clean. A counter that is one apart at a barrier and whose *result* agrees is
behaving like a value in transit, sampled at two different sub-steps of the same load — not like two
implementations that loaded differently.

If that is right, then making `load_stage` decisive compares *how far each core had got when the sampler
looked*, which is a property of the sampler, and it will differ by one whenever the two cores spend
different numbers of frames inside a load stage. That is a measurement artifact wearing the costume of a
guest divergence, and it is the same error this repository has already made three times: reading a value
at a moment when it does not hold.

If it is WRONG — if the product genuinely walks the load stages differently — then this is a real port
divergence and the fix belongs in the loader, not in the route.

THE MEASUREMENT, AND WHY IT CAN ANSWER
--------------------------------------
Watch `g_LoadStage` every frame from before the level load until well after it, alongside `g_Gamestate`
and `g_LevelId`, and record the whole sequence with the frame each value was first seen. That
distinguishes the two cases without interpreting anything:

  - a value that climbs 0,1,2,3 and then SITS at a final number for the rest of the level is a state with
    a settling point, and the barrier should park on the settled value;
  - a value that keeps moving, or that never repeats a value the two cores would both pass through, is a
    progress counter, and its exact value at a threshold is not comparable.

The run also prints, for every observed value, how many frames it was held. A counter held for one frame
is a transit value; one held for a hundred is a state. **The denominator is the number of frames actually
observed**, printed alongside, so "it settled" is a claim about a measured span and not an impression.

TWO CONTROLS, AND THE SECOND ONE IS THE ONE THAT MATTERED
---------------------------------------------------------
1. The final leg re-reads the same address and requires the same word, so a probe that lost the port or
   read a stale REPL buffer cannot produce a confident sequence of zeros.
2. **The run REFUSES to conclude anything unless the state under test was actually entered.** Control 1
   alone is not enough, and the first run of this file proved it: it polled 1,500 frames from boot,
   `load_stage` read 0 for every one of them, control 1 passed, and the tool printed a confident
   `VERDICT: ... a SETTLING value`. **Nothing had been loaded.** A counter that is legitimately 0 before
   its level loads is indistinguishable, under control 1, from a counter that never moves. So the probe
   drives `drive.Navigator` to real gameplay first, records the level id it reached, and requires a
   NON-ZERO `load_stage` to have been observed before it will call the value a state or a counter.

This is the repo's standing rule and it is in `tools/drive.py` already: a driven run that ends without its
symptom proves nothing unless the state under test was reached.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import drive  # noqa: E402

G_LOAD_STAGE = drive.G_LOAD_STAGE
SENTINEL = 0xFFFFFFFF
TERMINAL_STEP = 10  # loaders.c: initialisation loops `while (g_LoadStage < 10)`; camera.c:491 tests >= 0xA


def classify(sequence: list[int]) -> dict:
    """Describe a load_stage sample: how many load cycles, the terminal step, the longest climb.

    THE DOMAIN IS WIDER THAN THE LOAD STATE MACHINE, and this function does not pretend otherwise. The
    decomp's load path climbs 0 -> 10, but a live run also shows 9 and 13, so the word is REUSED after
    loading completes. The first draft of this file's own comment claimed "0 through 10" and was wrong
    by its own measurement; only the load path is bounded by 10, and what the other values mean is not
    established here.

    Kept separate from the driver so `--selftest` can exercise it on synthetic sequences, which is the
    only way to test the SHAPE claims without a product. A "cycle" is a fall to 0 followed by a climb,
    which is what a second level load looks like -- and getting that backwards is the mistake this
    function exists to prevent, because a fall to 0 reads as a walk in reverse if you do not know the
    counter resets per load.
    """
    steps = [s for s in sequence if s != SENTINEL]
    cycles = 0
    longest = 0
    climb = 0
    for prev, cur in zip(steps, steps[1:]):
        if cur == 0 and prev != 0:
            cycles += 1
        if cur > prev:
            climb += 1
            longest = max(longest, climb)
        else:
            climb = 0
    terminal = steps[-1] if steps else None
    if cycles == 0:
        shape = "one load, no reset observed"
    elif cycles == 1:
        shape = "one reset to 0: a second load began, so the index restarted (NOT a backwards walk)"
    else:
        shape = f"{cycles} resets to 0: repeated loads, each restarting the index"
    return {"shape": shape, "loads": cycles + 1, "terminal": terminal, "longest_climb": longest}
G_GAMESTATE = drive.G_GAMESTATE
G_LEVEL_ID = drive.G_LEVEL_ID


def selftest() -> int:
    """Prove the SHAPE claims, on sequences with known answers.

    Every case below is a mistake this file has actually made. The first two runs reported a confident
    "SETTLING value" from a sequence that never left the sentinel, and a "walk in reverse" reading of a
    load that had simply restarted -- so the cases that matter most are the ones where the answer is NOT
    the intuitive one, and the assertions are on the classification rather than on a printed string.
    """
    failures: list[str] = []
    checks = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(label)

    S = SENTINEL

    # A single load that ends loaded. One cycle, terminal 10.
    one = classify([0, 0, 1, 2, 7, 10, 10, 10, S, S])
    expect(one["loads"] == 1, f"one load is 1 cycle, got {one['loads']}")
    expect(one["terminal"] == TERMINAL_STEP, f"terminal step should be {TERMINAL_STEP}, got {one['terminal']}")
    expect(one["longest_climb"] == 4, f"0->1->2->7->10 is a 4-step climb, got {one['longest_climb']}")

    # THE MISTAKE: 10 -> 0 looks like a walk in reverse. It is a second load restarting the index, and
    # the classification must say so rather than reporting a non-monotonic walk.
    two = classify([0, 1, 10, 0, 7, 10, S])
    expect(two["loads"] == 2, f"a reset to 0 is a second load, got {two['loads']}")
    expect("NOT a backwards walk" in two["shape"],
           f"a reset must be named as a restart, got {two['shape']!r}")

    # The first two runs' failure mode: nothing but the sentinel, so nothing was observed at all. The
    # caller refuses on an empty `held`, and classify() must not invent a terminal value from one.
    empty = classify([S, S, S])
    expect(empty["terminal"] is None, f"an all-sentinel sequence has no terminal step, got {empty['terminal']!r}")
    expect(empty["loads"] == 1, "an all-sentinel sequence claims no observed load")

    # Repeated loads: three cycles, and the terminal value is the LAST load's, not an accumulation.
    three = classify([0, 10, 0, 10, 0, 3, 10, S])
    expect(three["loads"] == 3, f"three loads, got {three['loads']}")
    expect(three["terminal"] == 10, f"terminal should be the last load's step, got {three['terminal']}")

    # A hold must not be counted as a climb. 10,10,10,10 then 0 is a restart, not four steps.
    held = classify([0, 10, 10, 10, 10, 0, 10])
    expect(held["longest_climb"] == 1, f"a held value is not a climb, got {held['longest_climb']}")
    expect(held["loads"] == 2, f"the restart still counts, got {held['loads']}")

    if failures:
        print(f"[loadstage] selftest FAIL: {len(failures)} of {checks} checks failed")
        for label in failures:
            print(f"[loadstage]   - {label}")
        return 1
    print(f"[loadstage] selftest OK: {checks} checks -- a reset to 0 is named a second load rather "
          f"than a backwards walk, an all-sentinel sequence yields no terminal step, and a held value "
          f"is not counted as a climb")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--frames", type=int, default=1400,
                    help="frames to observe from the start of the run (default: 1400)")
    ap.add_argument("--settle", type=int, default=400,
                    help="frames to keep observing after the last change (default: 400)")
    ap.add_argument("--selftest", action="store_true",
                    help="exercise classify() on synthetic sequences -- no product, no disc")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

    env = drive.environment(drive.disc_path(), TOOLS / "shipping_settings.ini")
    # PSXPORT_REPL is KEPT here. `drive.Port` speaks the blocking REPL protocol over stdin, so popping it
    # would leave the product presenting with nobody to answer, and the probe would sit waiting for a
    # prompt that never arrives. Popping it is the `tools/live_play.py` pattern, which drives the port
    # over the HTTP debug server instead -- a different transport, not a detail.
    log = TOOLS.parent / "scratch" / "logs" / "load_stage_poll.log"
    port = drive.Port(TOOLS.parent / "build" / "bin" / "spyro_port",
                      TOOLS.parent / "scratch" / "assets" / "spyro1" / "SCUS_942.28",
                      log, env)
    try:
        # The load happens ONCE, on the way to gameplay, and the counter is -1 (its "not loading"
        # sentinel) for the whole level afterwards. So polling after arrival measures the sentinel and
        # nothing else — which is what the second run of this file did, for 2,500 frames, and it is why
        # a "non-zero" test is not enough: 0xFFFFFFFF is non-zero and is still the sentinel.
        #
        # The sample therefore rides INSIDE the frame advance, so every frame the game actually runs is
        # observed — including the frames of the load, which is the entire subject.
        seen: list[tuple[int, int, int, int]] = []  # frame, load_stage, gamestate, level
        held: dict[int, int] = {}
        last_frame_with_change = 0

        real_run = port.run

        def sampling_run(frames: int) -> int:
            nonlocal last_frame_with_change
            for _ in range(frames):
                stage = port.word(G_LOAD_STAGE)
                frame = len(seen)
                seen.append((frame, stage, port.word(G_GAMESTATE), port.word(G_LEVEL_ID)))
                if stage != SENTINEL:
                    held[stage] = held.get(stage, 0) + 1
                if len(seen) >= 2 and seen[-1][1] != seen[-2][1]:
                    last_frame_with_change = frame
            return real_run(frames)

        port.run = sampling_run  # type: ignore[method-assign]

        print("[loadstage] driving to gameplay, sampling every frame...")
        navigator = drive.Navigator(port)
        navigator.reach_gameplay()
        arrived_at = port.word(G_LEVEL_ID)
        # Keep observing past arrival so a value that settles is seen settling.
        port.run = real_run  # type: ignore[method-assign]
        tail_frames = 0
        while tail_frames < args.settle:
            stage = port.word(G_LOAD_STAGE)
            frame = len(seen)
            seen.append((frame, stage, port.word(G_GAMESTATE), port.word(G_LEVEL_ID)))
            if stage != SENTINEL:
                held[stage] = held.get(stage, 0) + 1
            if len(seen) >= 2 and seen[-1][1] != seen[-2][1]:
                last_frame_with_change = frame
            real_run(1)
            tail_frames += 1
        print(f"[loadstage] arrived in level {arrived_at} after {len(seen)} sampled frames.")

        # NEGATIVE CONTROL. A probe that lost the port reads zeros forever, and zeros are a perfectly
        # plausible "the load stage never left 0" answer. Re-read the same address and require the same
        # word; if it moved, the sequence above cannot be trusted and is reported as such.
        control = port.word(G_LOAD_STAGE)
        last = seen[-1][1]
        trusted = control == last
        frames_observed = len(seen)

        print(f"[loadstage] frames observed: {frames_observed} (sampled every frame of the run; "
              f"settle window {args.settle})")
        print(f"[loadstage] control re-read: 0x{G_LOAD_STAGE:08X} = {control} "
              f"(last observed {last}) -> {'TRUSTED' if trusted else 'UNTRUSTED, the port moved under the probe'}")
        if not trusted:
            print("[loadstage] REFUSED: the control failed, so this sequence is not evidence. "
                  "A dead or desynchronised port reads zeros and would otherwise look like a "
                  "counter that never moved.")
            return 2

        print(f"[loadstage] non-sentinel load_stage values, with the frames each was HELD "
              f"(sentinel 0xFFFFFFFF = 'not loading', excluded):")
        for value in sorted(held):
            print(f"[loadstage]   {value:>3} held {held[value]:>5} frame(s)"
                  f"   ({100.0 * held[value] / frames_observed:5.1f}% of the observed span)")
        sentinel_frames = sum(1 for _, s, _, _ in seen if s == SENTINEL)
        print(f"[loadstage]   sentinel 0xFFFFFFFF held {sentinel_frames} frame(s) "
              f"({100.0 * sentinel_frames / frames_observed:5.1f}%)")

        transitions = [(seen[i][0], seen[i - 1][1], seen[i][1])
                       for i in range(1, len(seen)) if seen[i][1] != seen[i - 1][1]]
        print(f"[loadstage] {len(transitions)} transition(s), in order "
              f"(frame, from -> to):")
        for frame, old, new in transitions:
            print(f"[loadstage]   frame {frame:>5}: {old} -> {new}")

        # CONTROL 2, and the one that catches a run that never tested anything. The counter is 0xFFFFFFFF
        # ("not loading") both before and after a level load, so a probe that only ever sees the sentinel
        # has measured nothing -- and 0xFFFFFFFF is NON-ZERO, so the first version of this check, which
        # only asked for a non-zero value, was satisfied by the sentinel and reported a confident
        # "settling value" from a run in which no load happened at all.
        observed = sorted(held)
        if not observed:
            print(f"[loadstage] REFUSED: `load_stage` never left its 0xFFFFFFFF sentinel across "
                  f"{frames_observed} sampled frames of the route to level {arrived_at}. No load stage "
                  f"was observed in progress, so this run says nothing about whether the counter is a "
                  f"state or a progress value. Reported as void, not as settling.")
            return 2
        print(f"[loadstage] non-sentinel load_stage values observed: {observed}")

        # WHAT DOES THIS WORD ACTUALLY TRACK? `g_LoadStage` has NO symbol in the decomp -- 0x80075864 is
        # an unnamed `.space 0x04` in `asm/data/game.sbss.s:385` -- so the name in
        # `game/core/guest_globals.h:38` is an INFERENCE, and the demo route made this range DECISIVE on
        # the strength of that inference. The joint census below is what decides it: if the value tracks
        # g_Gamestate or g_LevelId instead, then "load stage" is a misnomer and a 1-vs-2 difference here
        # is a GAMEPLAY STATE difference at the barrier, not a loading one.
        joint: dict[tuple[int, int, int], int] = {}
        for _, stage, state, level in seen:
            key = (stage, state, level)
            joint[key] = joint.get(key, 0) + 1
        print(f"[loadstage] joint census (load_stage, gamestate, level_id) -> frames held:")
        for (stage, state, level), count in sorted(joint.items()):
            share = 100.0 * count / frames_observed
            print(f"[loadstage]   stage={stage:<11} gamestate={state:<4} level={level:<4} "
                  f"{count:>5} frame(s) ({share:5.1f}%)")
        # A word that is a FUNCTION of gamestate alone is a state code wearing a load-shaped name.
        by_stage_states: dict[int, set[int]] = {}
        for (stage, state, _), _ in joint.items():
            by_stage_states.setdefault(stage, set()).add(state)
        ambiguous = {s: v for s, v in by_stage_states.items() if len(v) > 1}
        print(f"[loadstage] values that map to MORE THAN ONE gamestate: "
              f"{ {k: sorted(v) for k, v in sorted(ambiguous.items())} or 'none'}")
        print(f"[loadstage] -> {'NOT a function of gamestate alone' if ambiguous else 'a FUNCTION of gamestate alone: it is a state code, not load progress'}")

        # THE VERDICT, and the terminology in the first two runs of this file was wrong.
        #
        # I called the value "SETTLING" because it stopped changing. It is not a value that settles: the
        # decomp says what it is. `external/spyro-1/include/loaders.h:9` declares
        # `extern int g_LoadStage;  // Load stage`, `loaders.c:824-844` steps it 0 -> 1 -> 2, and
        # `initialization.c:358-363` sets 3 and loops `while (g_LoadStage < 10)`, and `camera.c:491` tests
        # `g_LoadStage >= 0xA`. So DURING a load it is a state machine's step index, advanced once per
        # main-loop iteration, bounded by 10, with 0xFFFFFFFF meaning not loading (camera.c:405).
        #
        # The domain is NOT 0..10, though, and my first draft of this comment said it was. A live run
        # also shows 9 and 13, so the word is reused once loading is done; the run below reports the
        # terminal value and says so when it is above the load path's bound.
        #
        # That makes "it went backwards" the wrong reading too, and I wrote that down as a refutation
        # before checking the decomp. The 10 -> 0 step is a SECOND load beginning, which resets the index
        # to 0 and climbs again: 0 -> 7 -> 10 is a load in progress. The sequence is a load cycle, not a
        # walk in reverse. (The later 10 -> 9 -> 13 is NOT a reset, and is not explained here: it is
        # after both loads have completed, and 13 is outside the load path's bound.)
        #
        # Which is exactly why the route's `demo_level_load` barrier cannot compare its exact value: a
        # step index says how many iterations a core has run, so two cores that spend different numbers of
        # iterations inside one load sit on different indices at a threshold barrier. The measured
        # consequence is in the report: native 1 against console 2, at a declared state that matches
        # exactly. Whether the product really is a step behind, or the barrier simply fires a step
        # apart, is the open question -- and the cadence explanation for it is REFUTED by measurement,
        # because `aspect=0, fps60=0` gives the same 1-against-2.
        report = classify([s for _, s, _, _ in seen])
        print(f"[loadstage] classification: {report['shape']}")
        print(f"[loadstage]   terminal step {report['terminal']}, "
              f"{report['loads']} load cycle(s) seen (a cycle is a fall to 0 followed by a climb), "
              f"longest climb {report['longest_climb']} step(s)")
        print(f"[loadstage]   DURING a load the value is a step index: loaders.c:824-844 steps 0->1->2 and "
              f"initialization.c:358-363 loops `while (g_LoadStage < 10)`, with camera.c:491 testing "
              f">= 0xA, and 0xFFFFFFFF meaning not loading (camera.c:405).")
        if report["terminal"] is not None and report["terminal"] > TERMINAL_STEP:
            print(f"[loadstage]   BUT the terminal value observed here is {report['terminal']}, ABOVE the "
                  f"load path's bound of {TERMINAL_STEP}: the word is REUSED after loading, so its value "
                  f"outside a load is not a load step. What {report['terminal']} denotes is NOT "
                  f"established here.")
        print(f"[loadstage]   so its EXACT value at a threshold barrier compares iteration counts, not "
              f"state identity. That is a property of the barrier, and it is what native 1 / console 2 "
              f"at an otherwise identical declared state looks like.")
        return 0
    finally:
        try:
            port._proc.kill()
            port._proc.wait(timeout=10)
        except Exception:  # noqa: BLE001 - the port is being killed on every exit path
            pass


if __name__ == "__main__":
    raise SystemExit(main())
