---
id: 121
title: Three actor producers emit primitives retail's walker never matches
status: open
symptom: on the last of 220 oracle frames of artisans-arrival, 0x80059F8C emits 12 primitives and retail matches 0 of them, sparkle 0x800584C4 emits 10 and retail matches 0, shadow 0x80059A48 emits 16 and retail matches 12
tags: render,actor,oracle,producer
created: 2026-09-19
updated: 2026-09-19
---

## Symptom

`tools/actor_oracle_diff.py`, `replays/gameplay/artisans-arrival.pad`, last of 220 oracle frames.
Primitives are matched against retail's own moby walker by the MULTISET of `(x,y,rgb)` vertices, so
a match means retail drew that exact geometry in that exact colour.

```
native primitives : 692     retail primitives : 670
matched : 664     retail only : 4     native only : 28     best horizontal shift +0

by producer (submitted / matched / recoloured / unmatched):
  0x8001F798: 357/357/0/ 0     actor draw
  0x80020F34:  75/ 75/0/ 0     secondary actor
  0x80022A2C:  41/ 39/2/ 0     shaded moby / gems
  0x80023AC4: 173/173/0/ 0     paired actor
  0x800580F4:   8/  8/0/ 0     glow
  0x800584C4:  10/  0/0/10     sparkle       <-- none matched
  0x80059A48:  16/ 12/0/ 4     Spyro shadow  <-- a quarter unmatched
  0x80059F8C:  12/  0/0/12     unidentified  <-- none matched
```

The denominator matters: five producers match retail exactly, so the instrument is not failing to
match in general. It reports the other answer on the same frame, for the same walker, in the same
call. That is what makes these three readings worth acting on.

## What is known about each

- **`0x80059F8C`, 12 of 12 unmatched.** The primitives are semi-transparent textured quads belonging
  to class 114 and class 10 records. The producer has no recovered name yet.
- **`0x800584C4` sparkle, 10 of 10 unmatched.** All ten are white lines.
- **`0x80059A48` Spyro's shadow, 4 of 16 unmatched.** The other 12 match exactly, so this is a
  partial disagreement within one producer rather than a wholly wrong producer.
- **Retail-only, 4 primitives.** All are `code 32`, `semi=1`, untextured. Nothing in the native
  carries their geometry, so something retail draws is simply absent.

## What this does NOT say

"Unmatched" means retail's walker did not draw that geometry in that colour. It does not mean the
primitive is wrong on screen, and it does not mean the player can see any of this. Three of these
producers draw small, bright, short-lived things -- sparkles, a shadow, a semi-transparent quad --
where a sub-pixel or single-step difference produces a total geometry mismatch and a nearly
invisible picture difference. Before treating any of these as a visible defect, find it in a paired
picture-oracle frame.

The `0x80022A2C` recoloured pair is in this class: both are one count of one channel,
`000067 -> 000066` and `0000A0 -> 00009F`. Do not chase those.

## Where to look next

1. Name `0x80059F8C`. Until the producer is identified, 12/12 unmatched cannot be read as either a
   missing feature or a harmless difference.
2. For the shadow, ask what distinguishes the 4 unmatched primitives from the 12 matched ones within
   the same producer on the same frame -- that comparison is free and it is a real discriminator.
3. For the 4 retail-only primitives, find which retail producer emits them. `code 32` semi-
   transparent untextured is a narrow enough signature to search for.

## Relation to other issues

Not the same as 0120 (an actor sorting against terrain) or 0105 (actor-vs-actor depth order). Those
two are about ORDER between primitives that both exist. This is about primitives that exist on one
side and not the other.
