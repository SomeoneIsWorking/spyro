# Start-to-skip map

This is the evidence boundary for making logos, loading screens, and scripted sequences skippable.
It deliberately does not equate "Start is down" with "jump to the next state": Start is also the
gameplay pause button, and several screens perform required I/O while their artwork is displayed.

## Boot logos and loading

The resident boot function `0x800127C0` owns the entire sequence. Its 143-line Ghidra decompile is
in `scratch/decomp/frameown.c`; the emitted body is `generated/shard_6.c`. In order, it performs:

1. eight fade-in iterations for the first uploaded logo;
2. three synchronous data loads, then a VBlank hold until `now - stamp >= 210`;
3. eight fade-out iterations;
4. eight fade-in iterations for the second uploaded logo;
5. sets loading phase `[0x80075864] = 3` and calls `0x80014564` until the phase reaches 10;
6. another VBlank hold until `now - stamp >= 210`;
7. eight fade-out iterations, then the display/frame-loop setup.

There is no pad read or Start test in `0x800127C0`. Therefore boot-logo skipping is a PC
enhancement, not a dormant guest branch. The safe semantic boundary is also narrower than "leave
boot": the 3→10 loop is required loading work and may not be bypassed. Start may shorten a completed
logo's hold/fade, but cannot skip the I/O phases or the final display setup.

`PSXPORT_DEBUG=skipmap` measures this live. The observer wraps `0x800127C0`, so `region=boot` is
derived from the function's actual dynamic lifetime rather than a guessed frame range. Each Start
edge and every boot-phase/stage-state change is uncapped; every 600 fields it prints the denominator
including `start_edges=0` when it scanned input and found none.

The boot-only enhancement is now implemented through that same exact lifetime. On the first boot
field it records the Start level as a baseline, so a button already held when boot begins cannot
skip. A later up→down edge advances `[0x800749E0]`, the guest's own VBlank counter, by `0xD2` — the
exact threshold both logo holds compare against. It does not branch around any guest code. Thus all
fade iterations, the phase 3→10 loading loop, and final display setup still run.

Runtime discriminators, same shipping binary:

- Idle: 437 boot fields scanned, 0 fresh edges, 0 advances, clean exit.
- Pulsed Start: first field was DOWN and suppressed as held-at-entry; two later fresh edges produced
  two advances. Loading phase still transitioned 0→4→8→10, boot exited cleanly after 74 fields, and
  later stage Start edges did not reach the boot enhancer. See `scratch/logs/bootskip-negative.log`
  and `scratch/logs/bootskip-positive.log`.
- `PSXPORT_SELFTEST=bootskip`: 9 checks covering inactive input, held-entry suppression, release,
  one fresh edge, non-repeating hold, denominators, and released-entry positive path.

## Title / attract sequence

The title/attract overlay has a real Start path and should keep using it. Claim C110 and issue 0027
establish the two input shapes in the resident `OV_5B800` image:

- `[0x80077380]` is held input; `0x8007AC48` tests Start while the title timer is armed.
- `[0x80077378]` is newly-pressed input; `0x8007B88C` tests Start/X in the sub-state-1 arm.
- the legitimate transition writes stage sub-state 2 and sub-sub-state 5 at
  `0x8007B8F0..0x8007B8F8`; it then reaches sub-state 3 through the guest's memory-card completion
  chain. That chain is now functional (issue 0027 resolution), so no PC state poke is justified.

Repeated synthetic Start pulses are not a shipping skip mechanism: once the sequence hands off to
gameplay, another pulse opens the pause screen. A shipping implementation must consume one host edge
inside a positively identified skippable state and release it before the next state reads input.

## Still unclassified

Stage mode 14 is now classified: it is recorded/demo playback, and Start handling is already guest
owned. `0x800331AC` advances the playback cursor each frame. When state `[0x8007566C] == 1`, it tests
held input `[0x80077380] & 0x840` (Start or Cross). Once the cursor is at least 241 and before the
last 32 samples, that input rewrites the cursor to `cursor / 2 + 16`, accelerating toward the same
natural terminal condition. It does not jump state. When `cursor >= sample_count * 2`, the function
calls the natural completion writer `0x8002D440`; that body performs the cleanup and writes stage
mode 13 plus the appropriate stage-state handoff. A native Start transition here would duplicate and
potentially conflict with a mechanism the game already has, so none is installed.

Live replay `scratch/logs/skipmap-replay-play.log` reaches mode 14 at field 1912 and leaves it through
the normal path at field 2183 while receiving Start edges. It then traverses another phase-driven
load and reaches gameplay modes 0/2. This also demonstrates why a global Start-to-next-state rule is
invalid: the same replay continues generating Start edges in modes 0/2, where Start is gameplay UI.

The observed loading surface is stage 13/sub 3 with `[0x80075864]` progressing through 0/1/3/4/5/6/7
and later 8/9/10/11/12/13. Those are actual streaming/load phases (`0x80032B08` / `0x80014564`), not
a presentation timer: the run cannot bypass them without skipping required I/O. No independently
owned "loading overlay finished displaying" transition has yet been observed, so no loading skip is
installed. The next safe target requires a run that distinguishes load completion from a subsequent
presentation-only hold and traces that hold's natural writer.
