# 0106 — Walking up to a portal aborts: the near-portal family emits no faces

Status: open
Affects: `docs/project-state.md` — homeworld portal presentation; blocks reaching a level on foot,
and with it live observation of the stage-10 return-home cancellation (`0104` is unrelated).

## What happens

Driving Spyro toward an Artisans portal (`tools/drive.py gameplay --seek-portal`) reaches distance
`0x13CC` and the frame aborts:

    NATIVE RENDER NOT IMPLEMENTED — stage selector = 0
    (cyclorama producer 0x80050BD0 refused its atomic recipe)

## The measurement

`prepareFrame` classifies a portal at `distance <= 0x3000` as `NearFamilyUnsupported`, which routes
its recipe to the near producer `0x8004F4BC` instead of the mid/far `0x80050240`. The recipe is
built from the SAME contracted aperture half-planes as the mid/far path, on the assumption recorded
in `cyclorama_portal_mesh_recipe.cpp` that the near renderer consumes them unchanged. Measured, with
the `fieldsky` per-draw report added in this change:

    near draw 0: portal=2 frame=near family unsupported/near_portal_family recipe=valid empty/none
      faces=0 distance=5068 mask=true objects=10/35 candidates=893 box_rejected=155
      aperture_rejected=738 accepted=0

So the recipe DID look: 10 of 35 objects survived the depth cull and 893 candidate triangles were
examined. Every one was then discarded — 738 of them by the aperture half-planes specifically, and
none survived.

That does NOT generalise to "the near family never works": `docs/project-state.md` records a gate-0
teleport whose near-family recipe produced 94 clipped triangles through the same aperture. So the
aperture is not always empty, and the open question is what distinguishes the two camera poses —
the walked approach at `0x13CC` versus the teleport — rather than whether the near path runs at all.

The abort itself is a second, separable fact: `build` reports `ValidEmpty` for an
all-clipped recipe (`refusal = "none"`, a legitimate outcome), but
`cyclorama_portal_submitter::prepare` only skips a draw when the frame AND the recipe are both
`ValidEmpty`. A live frame whose recipe emits zero faces is read as `InvalidRecipe`. Do not "fix"
this by accepting the empty recipe: an empty near portal in the player's face is wrong output, and
accepting it would replace a loud abort with a silently missing portal.

## Next discriminator

Compare the two poses directly: dump `frame.edges`, the clip rectangle, and the projected aperture
points for the teleport frame that emitted 94 triangles and for this walked frame that emitted 0.
If the walked frame's half-planes enclose no screen area, the aperture construction is the fault; if
they enclose area the geometry misses, the near family's clip source is, and what `0x8004F4BC`
actually clips against must be recovered from the retail routine rather than from the mid/far
transcription. `0x80050BD0` selects between the two families at the same `0x3000` threshold, so the
selection itself is already recovered.
