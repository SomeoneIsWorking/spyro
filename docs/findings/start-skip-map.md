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

`PSXPORT_DEBUG=skipmap` now measures this live. The observer wraps `0x800127C0`, so `region=boot` is
derived from the function's actual dynamic lifetime rather than a guessed frame range. Each Start
edge and every boot-phase/stage-state change is uncapped; every 600 fields it prints the denominator
including `start_edges=0` when it scanned input and found none.

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

The level scripted/cutscene states and in-level loading overlays have not yet been mapped to exact
guest completion transitions. They are not safe to special-case by stage number alone. The next
measurement is a played/replayed run with `PSXPORT_DEBUG=skipmap`, correlating each Start edge with
the uncapped `(stage mode, sub, sub-sub, loading phase)` transitions, followed by a writer trace on
the transition that an unskipped sequence takes naturally.

