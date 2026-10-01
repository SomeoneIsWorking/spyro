---
id: 167
title: Spyro 2 and Spyro 3 reach playable gameplay from the title screen through Lightrec, and the player moves under pad input
status: resolved
symptom: no input had ever been driven past either title screen
tags: spyro2,spyro3,gameplay,drive
created: 2026-10-01
updated: 2026-10-01
---

## Answer

`uv run --frozen python tools/title_route.py --title spyro2|spyro3 --shot-dir scratch/play/<t>` observes the guest's
own game-state word (facts and the instruction bytes they come from: `tools/title_profile.py`), taps Start at the
title and then Cross/Start every 150 fields until the playing state, and refuses unless the title and loading states
were seen first. It then reads the player's (x, y, z) triple, walks, lets the player rest, jumps, and refuses unless x/y
moved by at least 100 and z rose. Both pass headless and silent through pad edges only (nothing written to guest memory),
on psxport `s23-play` `2bb8d2f6` (rebased on `24316d27`):

| title | fields to gameplay | states seen | settled | after 60 fields walking | at rest | after a 16-field jump |
|---|---|---|---|---|---|---|
| Spyro 3 `0x8006E020` | 4740 | 0 11 5 6 5 6 5 6 5 3 0 | (39414, 13687, 18388) | (39421, 15359, 17929) | (39650, 15683, 18032) | z 18032 to 18463 |
| Spyro 2 `0x80067EE4` | 3190 | 0 11 5 6 5 6 5 3 0 | (68710, 33167, 9831) | (69350, 32710, 9831) | (70100, 32213, 9831) | z 9831 to 10282 |

Captures (opened and read): `scratch/play/spyro3r/{settled,moved}.ppm` show Spyro in a green valley with the HUD (lives 4, gem
and key counters) at Sunrise Spring's start, then turned and moved with Sparx beside him; `scratch/play/spyro2r/...` show
Spyro on grass facing a brown character with Sparx, then turned toward the camera. The Spyro 3 position was found by three
RAM dumps (idle, walk, walk and turn) and a fourth pair for the jump; the triple repeats at `+0x0C` as the previous
position, and the z word `0x8006E028` is read-modify-written at `0x80012354/0x80012364`. The Spyro 2 z word `0x80067EEC`
is reached through the base pointer built at `0x80016AF4`, so a lui+offset scan of `0x80067EEC` reports zero sites
(the dead-tap shape; the jump moving it 9831 to 10054 to 10348 is what identifies it).

## The path, and the blockers it found

Spyro 3: stock CdRead completions were instant (issue 0164, with the lost CD delivery behind a VBlank claim) and the
memory card HLE had read and write swapped (0165). Spyro 2: a native Pause never stopped the controller (0166). The
intro cutscenes (gamestate 6) are real authored presentation and are skipped by Start/Cross, not by a write.

## Not claimed

- No oracle comparison was attempted for either title (no independent state to diff against yet).
- Spyro 2: the first walk forward enters a conversation (gamestate 1). SUPERSEDED by issue 0168: the text draws; the
  early capture caught the box before its typewriter reveal. `tools/title_conversation.py` exercises it.
- Spyro 2's 59 diagnostic fallback blocks (`0x8005FFFC` false positive) are fixed in `shared/lightrec` `4696481` (issue 0168).
- The first level was identified only by the picture; issue 0168 reads it from guest words: Glimmer, id 11.
- No frame-time, audio, widescreen or interpolation evidence; Spyro 3's 4740 fields spend about 20 seconds of wall time.
