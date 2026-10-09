---
id: 178
title: Spyro 2 widescreen changes where the walk ends
status: open
symptom: the same walk ends at x 69231 at 4:3 and 69112 at 16:9
tags: spyro2,widescreen,determinism
created: 2026-10-08
updated: 2026-10-10
---

Same route and input, `scratch/record/route.py --walk 60`: the player ends at x 69231 at 4:3 and
69112 at 16:9, on the Record path and on the old Gte path alike. On Record the guest's projection
centre and draw area are retail's at both aspects, so the guest-visible difference is elsewhere;
not traced yet. Widescreen must not change guest behaviour, so this is a defect.

## Reproduction (2026-10-10)

`tools/fps60_control_settings.ini` (aspect 0) and `tools/wide_only_control_settings.ini` (aspect 1), same disc, blank
card, `scratch/record/route.py`-style route (Start at the title, observed-state presses, walk 60 fields):
the guest reaches gameplay at field 3960 at 4:3 and 3950 at 16:9 and the walk ends at (68958, 32985) and
(69179, 32853). Each aspect repeats itself exactly (two 4:3 runs: identical `fielddigest` for 4245 fields).

## First differing words

Guest RAM dumped (`dumpram`) at the same field of both runs and compared word by word:

1. Field 604 (title screen): 0x80067030 (the packet cursor), 0x8006717C (`[0x80067034]-[0x80067030]`, stored
   by 0x800150CC in the title draw) and the packet buffers 0x801A2000..0x801DB458 differ (25,040 words). The
   16:9 field holds 2744 more packet bytes: the native terrain drawer (`guest_terrain_drawer.cpp`,
   `horizontalMargin`) emits the margin polygons into the guest's own packet cursor. Rendering-only words.
2. Field 983: the first word outside the packet arena, 0x80093C70, byte +1: 1 at 4:3, 2 at 16:9. It is the
   classification byte of a moby record (+0x4D, the 0xA5 of FUN_80043858's decompile) written by the native
   moby visibility walk (`guest_moby_visibility.cpp:MobyWalk`, `slopeFor`/`insideFrustum` with the widened
   slope: 2 = fully inside the frustum). The same value is written into the render-list entry the guest's moby
   drawers read to pick their clipped or unclipped path.
3. Field 1533 (title idle, a blank card being formatted): three words at 0x8019E24C/E84C/EE4C and the copies at
   0x80067E54..5C differ by exactly +0x7E, a stopwatch-derived value; the guest's clock was 126 cycles apart
   at that read.
4. Field 3916: `emulatedCpuTicks` differs (2213408803 vs 2213281441) at the level load, the guest's frame
   counters (0x80066354, 0x80066618, 0x80066F9C) differ by one from field 3920, and the player's later
   position follows from the walk starting a field earlier relative to the route's input.

## Cause

`game/render/field/guest_moby_visibility.cpp:MobyWalk` and `game/render/terrain/guest_terrain_drawer.cpp:Drawer`
widen their horizontal plane tests and write the widened results into guest memory the guest then executes
against: the moby class byte and render list make the guest's own moby drawers take a different instruction
path, and the extra terrain packets change what the guest's per-field code touches. Guest cycles are the
stopwatch the guest reads (root counter 2, frame boundaries, CD deadlines), so any aspect-dependent guest work
moves guest time; the clocks first part at the level load (field 3916), which is where the walk's start shifts by a field. How the load turns a mid-field offset into a field is not traced.

## Measured isolation (experiments on the tree, all reverted)

| build | 16:9 vs 4:3 walk end | RAM at field 1600/3500 outside the packet arena |
|---|---|---|
| shipping | 69060,32924 vs 68873,33035 (differ) | 9 words (the +0x7E stopwatch triple, copies) |
| moby walk forced to the retail slope | 68873,33035 both (equal) | the same 9 words |
| retail class byte in the record only | differ | |
| retail class in record and render-list entry, widened cull | 68873,33035 both (equal) | the same 9 words |
| moby retail slope and terrain margin 0 | 68873,33035 both (equal) | 0 words at 1000, 1500, 1600, 1700 |

So the class the guest's drawers read (not the record byte, not the list membership) moves the walk on this
route, and the terrain margin moves the guest's clock (the stopwatch words) without moving the walk here. Both
are the same defect: widened work is visible to the simulation because it is done by, or into, the guest.

## Proper fix (not done)

Retail geometry must be the only geometry the guest sees, and the margin content must be produced beside it:
the walk and the drawer write retail classification, list and packets into guest memory, and a second output
(a side packet list with its OT slot and key) carries what only the margin shows, composed into the Record
canvas at present (`FrameRecord` gets a margin-entry source; `RecordRasterizer` draws it into the canvas). The
terrain drawer already runs natively, so its margin polygons can go to the side list at once. The mobys are
drawn by guest code (0x8003A0D8, 0x8004055C, ...): their margin entries need those drawers run in an
off-clock sandbox (GTE, scratchpad and registers restored, `guestInstructionTicks` and the device clock
rolled back) over the margin-only list, or native moby drawers (the Spyro 3 actor path has them). That is a
framework API plus two producers plus the keying for fps60 (issue 0176); it also retires the shared
widened-slope walk. Stopgap options (retail class everywhere, or margin 0) would remove the margin objects
and terrain from the canvas, which S026 exists to show, so none was applied.

Instruments: `scratch/s2-determinism/{route_dump.py,mdiff.py}` (route with RAM dumps at chosen fields; masked
word diff).
