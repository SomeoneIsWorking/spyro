# Spyro 2

`SCUS_944.25` (USA), authenticated by `executable.json`. `Spyro2Runtime` is a direct runtime: it binds
no Spyro 1 config, hooks or renderer. Lightrec caches and overrides are keyed by image identity plus
address, so Spyro 1's identical addresses never select this title's behaviour.

## Boot and frame loop

The shared `spyro::BootPrefixFrameDriver` (facts in `core/spyro2_boot_facts.h`) runs the retail boot
prefix `0x80011E9C` to its return, then game main's pair each step: update `0x8001B140`, draw
`0x800156FC`. Every guest VSync reaches the framework's libetc boundary (`0x80058EDC` never runs)
and comes back as a frame-boundary exit the driver answers with one field; each step presents one
field. The route `tools/title_route.py --title spyro2` reaches Glimmer gameplay at field 3950.

## Native overrides

| address | owner | what |
|---|---|---|
| libgte projection leaves | `spyro::GuestWidescreenOwner` | the widening decision |
| `0x80023BB4` | `game/render/terrain/guest_terrain_*` | the terrain drawer, every pass; a Record producer |
| `0x8001B2A8` | `render/spyro2_depth_bins.*` | the ordering-table flatten: assigns each packet its bin, then runs the original |
| `0x80043858` | `game/render/guest_moby_*` | the moby visibility walk |
| `0x8005251C`, `0x800520CC` | `render/spyro2_hud_anchor.*` | HUD counters and sprites anchored at the widened edges |

## Presentation

Record path: the guest's GP0 output is replayed from the frame record, and with fps60 the
in-between composes each producer's render with the record. The terrain drawer saves its field's
camera and sector visibility and `guest_terrain::TerrainStateProducer` redraws the terrain from them at
any t; `render/spyro2_depth_bins.*` gives every packet its ordering-table bin;
`core/spyro2_frame_cut.*` declares a cut on a new game state or level.
Widescreen keeps the guest's projection and draw area and widens the canvas. Status and open items:
`docs/project-state.md` S026/S028.
