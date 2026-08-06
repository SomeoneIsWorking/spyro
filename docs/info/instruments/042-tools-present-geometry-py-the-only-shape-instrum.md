---
id: I042
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/present_geometry.py — the only SHAPE instrument in this workspace: measures the presented picture's ASPECT. Every other capture check here (coverage %, colour count, brightness, tile richness) is INVARIANT under an aspect bug. Refuses (rc 3) when black margins make band-vs-picture ambiguous; give it --active/--display or --guest-frame (the frame's own drawn extent) for a verdict.

## Validated by

python3 tools/present_geometry.py --selftest = 16/16. Runs BOTH directions on synthetic frames: fills-sink 4:3 -> OK rc0; fills-sink 1.600x -> STRETCHED rc1; all-black -> REFUSED rc2; spyro-shaped (960x450 picture in a 960x720 sink, only 224/240 guest lines drawn) with NO guest info -> AMBIGUOUS rc3 (the OLD spider1 copy printed a confident STRETCHED 1.714x on that exact frame); the SAME frame with --active 512x224 --display 512x240 -> STRETCHED 1.600x rc1; and the NEGATIVE CONTROL, the FIXED present with the SAME flags -> OK rc0. Mutation-tested: 3 injected defects (band-treated-as-picture, off-by-one band edge, Paeth off-by-one) each drop the selftest to 14-15/16.

## Known failure modes

- **It CANNOT separate "black because the game drew black" from "black because it is a letterbox
  bar" from pixels alone.** That is why it refuses (rc 3) instead of guessing. Spyro is exactly this
  case: the guest draws 224 of 240 display lines (INHERITED, not measured here — I checked only that
  it is arithmetically consistent, 1.714/1.600 = 240/224 exactly, and both those numbers came from
  the same source; measure it with --guest-frame rather than trusting it), so every real spyro
  present will be AMBIGUOUS
  unless you pass `--active 512x224 --display 512x240` (or `--guest-frame <guest fb dump>`). Getting
  a bare rc-3 from a spyro frame is the tool working, not the tool failing.
- **The `--active` correction assumes the present is a UNIFORM SCALE of the guest display rect.** If
  the presenter crops, pans, or scales the axes differently inside the picture rect, the correction is
  silently wrong and the tool cannot detect it.
- **It is a geometry check on the FRAME, not on the CONTENT.** It cannot tell "correctly 4:3" from
  "the game happens to be drawing a square thing".
- **DUPLICATED FILE, and one copy is STALE.** This copy and `Tomba2Engine/tools/present_geometry.py`
  are byte-identical. `spider1/tools/present_geometry.py` is the ORIGINAL and has NOT been updated —
  it still prints a confident band-only aspect (it says STRETCHED 1.714x on a frame this copy
  resolves to 1.600x). Fix it with `cp spyro/tools/present_geometry.py spider1/tools/`. Before
  trusting a number from ANY copy run `md5sum */tools/present_geometry.py` from ~/repo/psx — no hash
  is quoted here on purpose, because a hand-copied hash rots on the next edit. The file's correct
  home is `external/psxport/tools/`, which needs a coord claim to do.
