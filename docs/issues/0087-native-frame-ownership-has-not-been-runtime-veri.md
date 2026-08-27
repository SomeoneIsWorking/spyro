---
id: 87
title: Native frame ownership has not been runtime-verified after guest VSync removal
status: resolved
symptom: Static build passes, but no launch has yet shown the resumable native boot reaches gameplay without hitting the mandatory VSync trap.
tags: frame,vsync,boot,native,verification
created: 2026-08-27
updated: 2026-08-27
---

# Native frame ownership has not been runtime-verified after guest VSync removal

status: resolved
tags: frame, vsync, boot, native, verification
state-items: S004

## Symptom

The title now declares libetc VSync `0x8005DBC4` as a fatal adapter HLE trap and removes the success override at helper `0x8005DD0C`. Static compilation cannot establish that every reached product caller has been bypassed.

## Implemented boundary

`Spyro1FrameDriver` is finite and is called once by the framework shell. `BootSequence` transcribes the non-VSync effects of boot `0x800127C0`/`0x8001286C` into resumable logo, hold, load-state, and finalization phases. GameConfig declares `.hle.vsyncTrap = 0x8005DBC4`; helper `0x8005DD0C` is not registered. Existing native CD loader owners bypass their retained libcd read-start chains; any unexpectedly reached legacy chain fails at the same VSync trap instead of receiving a success shim.

The first authorized product launch on 2026-08-27 proved the trap is live and found the next missing owner before the first boot field: `0x800122A8 -> 0x8005F8F8 -> 0x80061820 -> 0x80062090 -> VSync(-1)`. `0x80062090` is PsyQ libgpu's timeout arm; its retained body stores the field counter plus 240 at `0x80074B7C` and clears `0x80074B80`. Its pair `0x800620C4` also queries VSync before checking the deadline. The measured pair and globals now bind to psxport's synchronous-GPU timeout owners, which preserve the guest-visible timeout state without dispatching either retained VSync caller. Runtime proof must continue from the next reached boundary.

The next launch passed that boundary and found the synchronous WAD loader still super-calling its retained libcd path (`0x80016500 -> 0x8006606C -> VSync(-1)`). Both measured game-level loaders now own their transfer bookkeeping as well as the byte copy, leaving the retained bodies as non-product A/B oracles. The following run completed all 436 native boot fields, seven sync loads and five async loads, armed the host clock, entered stage 13, and rendered the first native title frame without reaching the VSync trap. It then exposed and removed a false verifier invariant that demanded two scheduler calls inside every logic iteration; retail's actual two-field throttle is against the previous guest stamp in `nativeFrameEnd`, so the first gameplay frame legitimately needed one new field.

## Verification result

The real `SCUS_942.28` product path is now exercised. Boot fields cross the same framework
`FramePresenter` fence as gameplay, zero-field asset/finalization transitions are folded into an
adjacent visible boot step, and a host turn delivers only guest timing/callback state while the outer
finite frame remains the sole display owner. The dedicated Clang build passed 34/34 CTests.

`scratch/logs/agent-spyro-wide-capture-600.stdout.log` exited 0 at
`PSXPORT_NATIVE_FRAMES=800`, reached native stage 13 after the 436-field boot, and reported the
frame-loop contract satisfied across 182 logic frames. It contains no `GUEST VSYNC VIOLATION`, no
FrameDriver fence violation, and three native producer rows totaling 262,789 attributed primitives.
The same run announced `aspect=1`, `wide_engine=1`, `native_width=512`, `render_width=684`.
`scratch/screenshots/present_600.ppm` is a real 960x720 picture (69.7% non-black, 3,022 colors), not
a counter-only assertion.

Reference/GTE rendering remains deliberately fail-fast. Static enumeration finds 22 direct resident callers and 82 call sites to `0x8005DBC4`. The render subset is not only outer driver `0x8001ED5C`: eleven stage render arms carry their own VSync display tails. A future diagnostic split must own the selected arm's tail as one family; bypassing only the outer driver would still trap inside that arm.

### Resolution (2026-08-27)
Resolved on the real SCUS_942.28 product path after routing boot fields through FramePresenter, folding zero-field boot transitions into adjacent presented steps, and making host-turn delivery timing-only. Clang build agent-spyro then passed 34/34 CTests. scratch/logs/agent-spyro-wide-capture-600.stdout.log exited 0 at the 800-field cap, reached stage 13, reported one native frame contract, no guest VSync violation, aspect=1 / wide_engine=1 / 512->684 projection, and three native producer rows. scratch/screenshots/present_600.ppm is a real 960x720 stage-13 picture (69.7% non-black, 3022 colors). The retained reference/GTE renderer remains intentionally fail-fast and is separate work.
