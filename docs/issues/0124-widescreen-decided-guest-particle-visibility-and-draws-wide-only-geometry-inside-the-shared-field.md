---
id: 124
title: Widescreen decided guest particle visibility, and still draws wide-only geometry inside the shared field of view
status: partially-fixed
symptom: the same deterministic gameplay frame emits 8 field-particle primitives at 4:3 and 20 at 16:9, and a solid (248,96,0) quad appears 261 px deep inside the shared field of view where the 4:3 frame draws grass
state_items: S019
tags: widescreen,particles,guest-state,parity
created: 2026-09-19
---

## How this was found

S019's gap said "only the courtyard and the intro card are covered by captures". Widening that
sample with `tools/drive.py gameplay --seek-class 83 --seek-arrived N` (deterministic: the same
invocation twice produced a byte-identical PPM) over six distinct scenes, every pair reads WIDENED
rather than STRETCHED by `external/psxport/tools/port/widescreen_pair.py`:

| scene | best translation | stretch | separation |
|---|---|---|---|
| seek d=2500 | 7.36 at dx=+86 | 76.00 | 10.3x |
| seek d=1500 | 11.10 at dx=+86 | 71.14 | 6.4x |
| seek d=800 | 15.13 at dx=+86 | 86.87 | 5.7x |
| hold left 90f | 10.16 at dx=+86 | 46.25 | 4.6x |
| hold right 90f | 9.38 at dx=+86 | 40.94 | 4.4x |
| hold up 120f | 10.86 at dx=+86 | 55.26 | 5.1x |

(`d=4000` and `d=2500` came back byte-identical -- the seek saturates before 4000 -- so that pair is
one scene, not two. The discriminator's own `--selftest` passes 3/3, including reading a resampled
picture as STRETCHED, so it can still say the other thing.)

But comparing the two legs pixel by pixel over the shared field found content that only widescreen
draws.

## Defect 1: widescreen decided what the GUEST believes -- FIXED

Both particle producers wrote a visibility byte back into the guest's own particle record, computed
from the WIDENED horizontal window:

```cpp
const int clipRight = gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
const bool visible = ... && projected.sx > 0 && projected.sx < clipRight && ...;
core->mem_w8(point.address + 3u, visible ? 1u : 0u);   // fx_field_particles.cpp
core->mem_w8(particle.address + 3u, visible ? 1u : 0u); // field_particle_type2_submitter.cpp
```

Turning widescreen on therefore changed guest memory. That contradicts S019's recorded claim that
"widening the projection to 684 px perturbs **no** guest state the oracle observes" -- which was
true only because the oracle's declared ranges do not cover particle records. A clean oracle run is
not evidence about state nobody declared.

`game/render/particle_screen_space.{h,cpp}` now owns the horizontal policy for both producers and
separates the two questions: the guest's byte uses the guest's own 512-px window, and what the port
DRAWS uses the widened one. The mapping between them is exact rather than tuned -- only `ofx`
differs between the two projections, so subtracting that difference recovers the guest's x. The
module also removes a verbatim duplicate of the projection helper that existed in both producers.

Verified: the 4:3 capture is **byte-identical** before and after (md5
`0d75310b5286c78fea18f82e629c1fea`), and the widescreen particle count falls from 20 to 12. Twelve
against the 4:3 eight is expected and correct -- widescreen legitimately draws particles in the
extra side area; what was wrong was writing that decision into the guest.

## Defect 2: wide-only geometry inside the shared field -- STILL OPEN

The fix above does not account for the visible artifact, and this issue stays open for it.

A solid `(248,96,0)` quad is drawn in the 16:9 frame at narrow-coordinates x 57..203, y 96..142 --
inside the shared field of view -- where the 4:3 frame draws grass. 241 such pixels remain after the
particle fix (261 before), so the particle guest-write was not its cause.

It is NOT a misalignment, measured rather than eyeballed. Per-region best horizontal offset between
the two legs:

| region | best dx | MAE at that dx |
|---|---|---|
| background upper-left | +86 | 0.00 |
| foreground grass | +86 | 0.00 |
| background upper-right | +86 | 0.07 |
| Spyro's sprite box | +86 | **28.02** |

Every region aligns at the same +86, and the background is byte-identical there, so the widening
itself is clean and the actor is not displaced. Content genuinely differs only in that one box.

### Falsifier

Producer counts at the same frame, 4:3 -> 16:9, after the particle fix: terrain `8004EBA8` 425->515,
world `800258F0` 477->573, actor `8001F798` 320->388 (all correct -- that IS the widening),
`80022A2C` 76->76, `80023AC4` 184->184, `80059A48` 16->16, `800580F4` 8->8.

So the extra geometry in the shared field must come from terrain, the world scene, or the actor
producer. Bisect by producer: suppress each of the three in turn at 16:9 and re-measure the
wide-only orange pixel count. The one whose suppression takes it to zero owns the defect. Do not
guess from the colour -- two guesses have already been wrong in this investigation (the displacement
that was not one, and the particle write that was real but not this cause).
