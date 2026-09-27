# 0134 — the BLENDED environment animation form has no authored trigger in the levels the shipping route reaches

`docs/project-state.md` item **S020** records this hole, verbatim:

> The animation's BLENDED (GTE-interpolated) form is covered hermetically only; no live frame has
> exercised it yet.

`tests/test_world_animation.cpp:1-6` says the same thing from the other side: the live corpus
`scratch/raw/stage0_artisans_refusal.bin` "exercises the DIRECT form only: the Artisans frame that
blocked issue 0089 has two straight-copy channels and no blended one."

This issue answers the question that sentence does not ask. **Can a live frame exercise the BLENDED
form, and if not, why?** The answer is no, and the reason is a property of the authored level data
rather than of the port.

## The mechanism, read out of the image

`SCUS_942.28`, guest renderer `func_800258F0` at `0x800258F0`. Every word below was re-read from
the provisioned executable with `tools/probe_guest_disasm.py`, which reports 62183 of 62183 listed
main-image instructions agreeing with `file_offset = 0x800 + (addr - 0x80010000)`; each word was
then compared against the decompiled listing's own `/* FILEOFF VRAMADDR INSNWORD */` comment and the
two copies agree. The four channels repeat one shape at +0x3C bytes each:

| channel | meaning | set-slot load | factor load | `bgtz` decision | IR0 write | GTE command |
|---|---|---|---|---|---|---|
| 0 | LowPoly, LQ vertices | `0x80025BD0` | `0x80025BF4` | `0x80025C00` | `0x80025C40` | `INTPL` `0x1100984A` at `0x80025CC4` |
| 1 | LowColor, LQ colours | `0x80025D2C` | `0x80025D50` | `0x80025D5C` | `0x80025DD8` | `DPCS` `0x1000784A` at `0x80025E38` |
| 2 | HighPoly, HQ vertices | `0x80025E74` | `0x80025E98` | `0x80025EA4` | `0x80025EF0` | `INTPL` `0x1100984A` at `0x80025F80` |
| 3 | HighColor, HQ colours | `0x80025FE4` | `0x80026008` | `0x80026014` | `0x800260B4` | `DPCS` `0x1000784A` at `0x80026134`, `0x8002617C` |

The chain, for channel 0:

- `0x80025BB8` `sll $at,$gp,24` / `0x80025BC0` `srl $at,$at,24` — the channel index, and
  `0x80025BBC` `bltz` retires any index >= `0x80`;
- `0x80025BC8` `lui $v0,0x8008` / `0x80025BCC` `addiu $v0,$v0,-0x7AA0` — `0x80078560`,
  `g_EnvironmentAnimations`;
- `0x80025BD0` `lw $v0,0x14($v0)` — the channel's animation-set pointer, which the recovered struct
  (`external/spyro-1/include/environment.h:27-57`) puts in the field order the four 8-byte-strided
  loads walk: `+0x14` LowPoly, `+0x1C` LowColor, `+0x24` HighPoly, `+0x2C` HighColor. Each pointer
  is preceded by its OWN authored count at `+0x10`/`+0x18`/`+0x20`/`+0x28`;
- `0x80025BD4`-`0x80025BDC` — `animation = set[index]`;
- `0x80025BE4` `lbu $v1,2($at)` / `0x80025BE8` `addi $v0,$at,0xC` / `0x80025BEC` `sll $v1,$v1,3` /
  `0x80025BF0` `add $v0,$v0,$v1` — `keyframe = animation + 12 + animation[2]*8`;
- **`0x80025BF4` `lbu $a1,4($v0)`** (`04004590`) reads the blend factor, and
  **`0x80025C00` `bgtz $a1,0x80025C3C`** (`0E00A01C`) is the whole decision: factor > 0 takes the
  GTE arm, factor == 0 falls through to the straight `lw`/`sw` copy loop at `0x80025C1C`;
- the GTE arm sets `IR0 = factor << 4` at `0x80025C3C`/`0x80025C40` — a 12.4 fixed-point weight —
  then runs `INTPL` and repacks `MAC1..MAC3` (`0x80025CF0`-`0x80025D08`).

So the form is selected by exactly one authored byte, and `game/render/world_animation.cpp` reads it
the same way (`ir0 = header.factor << 4` at :171, `header.factor == 0 ? direct : blended` at :197
and :225). The port's decode is not what stands between a live frame and the blended form.

## The measurement

**The product's own counter, over three live runs.** `lucent::debug("fieldenv", "PASS animation
channels={} direct={} blended={} writes={}")` at `game/render/fx_field_environment.cpp:69-74`:

| run | route | `fieldenv` submits | submits that decoded a channel | channels decoded | direct | **blended** | refusals |
|---|---|---|---|---|---|---|---|
| `scratch/logs/blended-a.log` | `drive.py gameplay --after 600` | 370 | 1 | 4 | 4 | **0** | 0 |
| `scratch/logs/probe_blended_live2.log` | same route, `--watch-frames 1200` | 626 | 1 | 4 | 4 | **0** | 0 |
| `scratch/logs/probe_blended_p1.log` | `probe_blended_anim.py --live --seek-portal` | 229 | 9 | 14 | 14 | **0** | 0 |

**22 channels decoded across 1,225 live submits over three runs; 22 direct, 0 blended.** The last
run's per-submit distribution, which is the widest sample: six submits at `channels=1`, two at
`channels=2`, one at `channels=4`, and 220 at `channels=0`. The `writes` counts are
payload writes plus one retire write per channel (`world_animation.cpp:264`), so `channels=4
writes=52` is 48 payload writes plus 4 retires — **four sectors each running one channel, not four
channel indices on one sector**, because `Plan` accumulates across sectors.

**The authored data, censused from live RAM.** `tools/probe_blended_anim.py --live` drives the
shipping route, wraps `drive.Port.run` so every guest frame is observed, and captures the full 2 MB
RAM dump on every frame where the four animation-set pointers move. A dump is only censused when the
dump's OWN pointers equal what the REPL read one command earlier.

| level | frames the tuple held | authored counts (ch0/ch1/ch2/ch3) | keyframes read | nonzero blend factors |
|---|---|---|---|---|
| boot + attract, no environment | 6,336 | 0 / 0 / 0 / 0 | 0 | 0 |
| **10 (Artisans)** | 794 | **12 / 0 / 12 / 0** | 24 | **0** |
| 11 (after the portal crossing) | 218 | 0 / 0 / 0 / 0 | 0 | 0 |

7,348 frames observed one at a time, 12 RAM captures, **0 refused on the pointer cross-check, 0
producing no file.** Artisans authors 24 environment animations — 12 LowPoly and 12 HighPoly, and
**no colour animations at all** — and all 24 of their keyframes carry blend factor `0`. Level 11
authors none.

## Why the counter is zero rather than unreachable-in-principle

Three things were ruled out, and each is a claim that could have been wrong:

1. **The instrument never ran.** Ruled out by the positive and negative selftest cases (§ below) and
   by the fact that the same counter reports nonzero `direct` and nonzero `writes` on 22 live
   submits in the same runs.
2. **The port refuses before it can blend.** Ruled out by the same 22 submits: 0 `fieldenv`
   refusals, and `direct` incremented every time.
3. **The port's decode is wrong about which byte decides.** Ruled out against the image, not against
   the listing — the four `lbu 0x4(keyframe)` sites, the four `bgtz` sites, the four
   `mtc2 C2_IR0` writes and the two `INTPL` / two `DPCS` command words are all quoted above from
   bytes read out of the executable.

What remains is the authored byte itself. The census is bounded by each channel's own authored count
word, so "24 of 24" is a complete statement about the two levels reached, not a sample.

## What would reach it

The smallest change is not in the port — `world_animation.cpp` already implements the form
correctly and `tests/test_world_animation.cpp:158-191` proves it. It is **a level that authors a
nonzero blend factor byte.** Nothing else can create one without writing guest bytes, which would be
forging state rather than exercising it.

Concretely: drive a route to a level whose `m_LowColorAnimationCount` / `m_HighColorAnimationCount` /
`m_LowPolyAnimationCount` / `m_HighPolyAnimationCount` is nonzero and whose keyframe byte 4 is
nonzero, and

    uv run --frozen python tools/probe_blended_anim.py --live --seek-portal

reports it in the `blend factors` field of the census line for that level, and the run's `fieldenv`
line then reads `blended=N`. The tool is ready; the missing input is level coverage, not
instrumentation.

**This issue does NOT establish the state of the other levels.** Two levels of a homeworld with ~40
levels were censused. The colour channels are the most likely place for a nonzero factor — a colour
fade is what a blend is for — and they are unauthored in both levels reached, so the question is
entirely open for them.

## Instrument, and the four ways its zero can be manufactured

`tools/probe_blended_anim.py --selftest`, 5 cases, registered as CTest
`probe_blended_anim_selftest`:

| case | asserts | why it exists |
|---|---|---|
| positive | factor `0x13` is read, located, and returned as REACHABLE, over a fixture asserted to have been read | the other direction |
| negative | factor `0x00` reads as the direct form, over a fixture asserted to have been read | the direction this investigation actually needs |
| zero count | an authored count of 0 scans **nothing** | the fixed 128-index scan that preceded it read 6 fabricated entries out of a table Artisans never authored, 3 carrying a nonzero factor byte behind a zero payload size |
| truncated | a set pointer outside the dump is `NOT READ`, never factor 0 | a short read reported as zeros is how a "no blended form exists" result gets manufactured |
| refused entry | a nonzero factor behind `size=0` counts nonzero and is reported **UNREACHABLE** | the exact shape the Artisans capture produced; this is what keeps "the byte was nonzero" from being read as reachability |

Two further ways this zero could have been manufactured were caught live, by the pointer
cross-check, and are recorded because both produced a clean-looking wrong number first:

- an earlier revision read **contiguous word pairs** instead of the 8-byte-spaced slots and sampled
  two padding words that read as `0`, which reads exactly like an unset channel. The cross-check
  refused the capture instead of censusing it.
- an earlier revision checked for the dump **without advancing a frame**; the REPL reads commands
  between frames, so all 18 captures reported "produced no file" when the file was there one frame
  later.

## Relation to S020

S020's recorded hole is a statement about coverage, and it stays open: no live frame has exercised
BLENDED. What this issue adds is the reason, which is a fact about Spyro 1's authored level data
and can be checked per level in one run. It also corrects a detail the recorded text leaves open —
that the Artisans live corpus *could* have contained a blended channel but happened not to. It
cannot: all 24 of Artisans' environment animations have blend factor 0, and both colour channels are
unauthored there.
