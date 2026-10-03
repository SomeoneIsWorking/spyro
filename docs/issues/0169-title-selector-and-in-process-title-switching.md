---
id: 169
title: Title selector and in-process title switching (and the stale-global defects a second boot exposed)
status: resolved
symptom: The product booted one hard-coded title per process; there was no way to choose Spyro 1/2/3 or to return and pick another
tags: picker,selector,session,teardown,globals,asan,panels
created: 2026-10-01
updated: 2026-10-03
---

## Answer

`./run.sh` (zero arguments) now launches `spyro_port` with no argument, which opens an in-window selector
over the three catalog titles. An entry is enabled only when `scratch/assets/<slug>/<serial>` exists and
authenticates through `selectExecutableFile`; a disabled entry shows why (`Not provisioned`,
`Identity mismatch ...`). Pad/keyboard (Left/Right, Cross/Start) and the control channel
(`picker`, `pick <slug>`, `select <slug>|left|right`, `picker shot <path>`) choose; a running title
returns through the ESC menu row "Return to Title Selection" or the control command `session return`;
both only record a request in `SessionControl`, and the host ends the session by destroying its `Game`.
Nothing is written into guest state.

Structure: `ProductHost` loops `PickerSession` -> `TitleSession`. Each session owns its own `Game`;
destruction is the whole teardown. The explicit executable argument (`spyro_port <exe>`) is a maintainer
override that skips the selector and runs that one title; the launcher no longer has a title flag or
positional disc.

## The selector as three live panels (2026-10-02)

The selector is no longer a list: it is one window frame with a panel per AVAILABLE title, each panel
showing THAT title's own attract demo from its own live session.

- **One real session per panel.** `PanelSessions` owns a `TitleSession` per available title. Each is an
  RAII `Game` that boots the authenticated executable through the normal product path
  (`TitleSession::boot` -> `dc_boot_init`), and presents into the host's composite
  (`GpuVkState::PresentTarget::Pane`) instead of into the window. No video, no screenshots, no
  fast-forward, no written guest state.
- **One guest advances.** `PanelSessions::advance` runs the selected panel's step, or spends a bounded
  boot budget on the first panel that has no picture yet (selected first, so the title the player is on
  appears before the others catch up). Every other session is paused: it holds the frame it last
  presented and does not step, so it cannot be fast-forwarded later.
- **Boot on first need.** A session is booted the first time it is needed, so a panel with no picture is
  genuinely empty rather than a placeholder; `hasPicture()` is what the compositor asks.
- **Widths animate.** `PickerLayout` holds a per-panel share and walks it toward the target (selected
  ~50%, others ~25%, normalized so the panels always tile the surface). Panels are PARALLELOGRAMS: the
  slant is applied about each panel's own centre, so panel i's right edge and panel i+1's left edge are
  one line and the divider between them is cut once.
- **The picture is fitted, never stretched.** Each panel shows the session's own presented picture —
  the composite AFTER its letterbox, fade and source selection (`GpuVkState::lastPresented`) — mapped
  onto the panel's aspect-fitted parallelogram. The framework's `psxport::PaneCompositor` draws the
  parallelograms (an affine two-triangle pass, so slanted panels are exact), the solid divider lines and
  the screen text.
- **Grey when unselected.** An unselected pane is desaturated (0.85) and slightly under full brightness;
  the selected one is untouched.
- **Audio from the selected session only.** `SpuAudio::setOutputEnabled` claims the process's one audio
  device for exactly one session; an unclaimed session never opens a stream.
- **Confirm continues the same session.** `PanelSessions::confirm` destroys every other session, hands
  the chosen one back to `ProductHost`, restores its window route and gives it the player's pad. The
  confirm press is delivered to that guest as a real pad press (`pressOnce`), so the demo leaves for its
  title screen the way it does on the disc. `session return` and the ESC row still work, and returning
  rebuilds the picker from a fresh probe.
- **Headless.** `picker shot <path>` writes the COMPOSITED frame (no single session's present shot can
  see a frame the host assembled from three). `tools/picker_look.py` drives and captures it.

### Framework changes this required (psxport)

1. `runtime/psx/pane_composite.{h,cpp}` + `shaders_gpu/pane.{vert,frag}`: `psxport::PaneCompositor`, one
   window frame from several sessions' presented pictures. New generic capability, own files.
2. `GpuVkState::PresentTarget` and `setPresentImageSize` (`gpu_vk_internal.h`, `gpu_vk.cpp`,
   `gpu_vk_screen.cpp`): a session's present builds its picture and keeps it instead of blitting it to
   the window, at the size its host asked for. `plan_present` and its both-legs-identical test are
   untouched — the window is still the only leg-dependent step.
3. **Lightrec's JIT arena is now claimed by MACHINE, not per state**
   (`shared/lightrec` fork `fc30382`, pinned as `PSXPORT_LIGHTREC_REVISION`). liblightning keeps ONE
   process-global code arena in globals: `init_jit()` allocates it, `finish_jit()` frees it, and
   `lightrec_init`/`lightrec_destroy` called that pair per `lightrec_state`. Correct only while a
   process creates one machine. With several live, the second `init_jit` overwrote the globals
   (leaking the first arena) and the FIRST machine destroyed freed the arena every other machine was
   still emitting into and executing from: `double free or corruption`, in a backtrace pointing at a
   machine that had done nothing wrong. Follow-up `3e6d9c6`: the code pool's tlsf allocator is claimed
   with the arena too — N machines had been building one allocator each over the same mapped pool, so
   two of them could be handed the same bytes.
4. **The RmlUi library lifetime is the PROCESS's** (`runtime/psx/rmlui_overlay.cpp`). `Rml::Shutdown`
   is process-global and used to be called by whichever Game ended first; with several sessions the
   first panel destroyed on confirm shut RmlUi down under the session still using it. Now: the first
   overlay initialises and installs the process's one renderer and system interface, `Rml::Shutdown`
   runs once at process exit, each overlay removes ITS OWN contexts on shutdown, and each overlay's
   context NAMES are unique (`CreateContext` refuses a name already registered — the fixed
   `psxport_menu` name meant every overlay after the first lost its context, and the screen that
   rebuilds the selector after a title returned could not build its screen). The renderer is
   process-lifetime because Rml's render-interface pointer is: a per-overlay renderer was a
   use-after-free, since a second overlay's document drew through the first overlay's renderer, which
   that overlay's shutdown had deleted.
5. **The debug endpoint can change hands** (`DbgServer::claimEndpoint`). The endpoint is
   process-lifetime and a claim is per-Game: the selector Game claimed it, the selector built its
   panels out of it, confirming hands the chosen session to the window and the selector Game dies —
   after which the channel belonged to nobody and every command timed out on a healthy run. The host
   points the channel at the session that is now the product.
6. **The host field clock is per-Core** (`Core::hostTurn`, `runtime/psx/host_turn.cpp`). It was one
   process-global registration: the second session's registration was refused ("already registered"),
   and the first session's teardown cleared it out from under the sessions still running.
7. **The per-Core runtime, not the process's last-installed one** (`picture_announce.cpp`,
   `platform_hle.cpp`): three sessions each install their runtime in turn, and two post-construction
   reads consulted the global — the wide aspect announcement (every present) and the hardware-service
   window table (every registration). Both now read `core.runtime` / `game->runtime`.
8. **`Pad::setPlayerInputSuppressed`** (`pad_input.{h,cpp}`): a host that reads the pad itself claims
   the player's input, so one Left tap moves one selection instead of pressing a button in three demos.
   Applied before the record/replay service, so guest, recording and replay still agree.
9. **`SpuAudio::setOutputEnabled`** (`spu_audio.{h,cpp}`): see above.
10. `psx::ui::ChoiceScreen` grew `setHeading` / `setEntriesVisible` / `setBackdropOpaque` (and
    `ChoiceView::select`), so a host whose entries are pictures can drop the text list and the opaque
    backdrop and keep only the name of the selection and the control hint.

### Bugs this found, fixed at the cause

- A `%s` on the catalog's `std::string_view` slug in the control channel's reply: a segfault, found by
  running the picker test. The catalog's identity strings are views, not C strings.
- `PaneCompositor::composite` refused its own first frame (it checked for the readback buffer it creates
  two lines later).
- A panel's present route was applied before that session had a machine; the route is now HELD as state
  and applied at boot.

## Evidence

Headless, one process at a time, `PSXPORT_PRESENT_SINK=1280x720`, driven over the control channel
(`select`, `pick`, `picker shot`), captures in `scratch/picker/`:

- three full-height slanted panels, each cover-cropped to its own picture with no letterbox bands and no
  bleed between neighbours; thin gold slanted dividers; the selected panel widest and in full colour, the
  others desaturated and dimmed; each panel's name and hint in a band along its own bottom edge.
- every panel reaches a real picture before the selector settles: a panel is shown only once its session
  has a FILLED frame (a quarter of a 16x16 sample grid carries colour, so a publisher logo on black does
  not count) AND has run 320 steps past the first lit frame.
- `pick spyro2` hands Spyro 2's own live session to the window and it runs there; `session return`
  rebuilds the selector and the rebuilt selector runs the full walk again.
- The two multi-session faults below are gone: a confirm after walking all three panels, and a selector
  rebuilt after a title returned, both run clean for Spyro 1 and Spyro 2 (zero `signal`, `executor:error`
  or `host dispatch` lines in either run).

`test_picker_layout` and `test_title_picker`: 0 failures. `clang-format` and `clang-tidy` over every
touched unit: clean.

## What each panel shows, and how it is decided

- **The pane IS the panel.** A pane is drawn as the panel's exact parallelogram (`origin = x −slant/2`,
  `axisU = (w,0)`, `axisV = (slant,H)`), and the picture is cover-cropped to that shape by
  `PanelLayout::sourceCrop`. Growing the pane past the panel to fill it is what made one panel's picture
  bleed into its neighbour; cropping the SOURCE is what fills it without that.
- **A panel shows the last frame that HAD a picture.** `GpuVkState::retainFilledPresentImage()` copies
  the newest presented frame that passes the content test and holds it; the compositor samples that, so a
  fade, a load screen or a scene change between the guest's own screens cannot blank the selected panel
  (which is exactly what the selected panel was doing: black while its neighbours ran).
- **A panel is shown only when it carries a picture.** The content test is a FILLED FRAME (a quarter of
  a 16×16 grid carries colour) with LOCAL COHERENCE (neighbouring samples agree), so a publisher's logo on
  black and a field of uninitialised colourful VRAM both fail, and the panel also has to be past its own
  boot prefix.
- **"Is that picture the TITLE's, or its publisher card?" is the TITLE's question, not ours.**
  `TitleSession::showsTitlePicture()` asks the session's own `FrameDriver::pastBootPrefix()`, which for a
  Spyro title is its retail boot prefix (logos, publisher cards, loading screens) having returned. Pixel
  heuristics were tried here and are wrong twice over: Insomniac's sphere SPINS for minutes, so "it moved"
  cannot separate a card from a demo, and Spyro 1's demo over a wide landscape changes few cells, so
  "half the frame changed" cannot either — the first test hid every panel, the second showed cards. What
  a panel shows is the title's own business and the title answers it.
- **The rotation never stops.** It used to hand every frame to the selected panel once all panels had
  content, which froze the others wherever they were — and a title's opening cards leave only by RUNNING.
  Measured standalone, Spyro 2 spends the best part of a minute on its publisher card before its demo
  plays, so a paused panel advertised that card indefinitely. Every panel now gets a slice every turn: a
  background panel half the boot budget, the selected panel and any unready panel the whole of it. One
  guest still runs at a time.
- **A held frame is refreshed, not kept forever.** The hold exists so a fade or a load screen cannot blank
  a panel; it is not a way to stop time. Caching "this panel has content" forever froze every panel on the
  first picture it ever had — which for Spyro 2 and Spyro 3 was the publisher card, permanently, and made
  every capture byte-identical minutes apart. The probe is asked again on each visit, is skipped when the
  present has not advanced, and replaces a held frame at most once every fifteen of that session's own
  frames, so a panel's steady cost is one GPU→CPU→GPU round trip per quarter second of its own time and
  none at all while it is not being shown.
- **The guest's own border rows are cropped out.** Spyro 3 draws about fourteen black rows at the top and
  bottom of its 240 lines (measured from its own windowed shot). A consumer cropping to the present
  viewport showed them as bars in the middle of the panel, so `GpuVkState::measurePresentedContent()`
  measures the picture's own extent and the compositor samples that. The measurement only ever WIDENS:
  a dark scene cannot shrink the crop and zoom into the middle of the picture.
- **The panels bring themselves up together.** The boot rotation works whichever panel is furthest from
  showing a picture, one slice each, so the selector takes about as long as the slowest boot instead of
  the sum of all three. Working one panel all the way to its first picture first made the selector take
  149 picker frames per title with two thirds of the screen black.
- **The slant is a fraction of the WIDTH** (a tenth), because it is a horizontal displacement; as a
  fraction of the height it was a few dozen pixels on a 1280-wide window, which is a hairline.
- **ONLY THE SEAMS LEAN.** The leftmost panel's left edge and the rightmost panel's right edge are the
  window's own and stand vertical, so an end panel is a TRAPEZOID and a middle one a parallelogram. Every
  panel was a parallelogram before, leaning about its own centre, and that pushed the leftmost panel's
  bottom-left corner a half slant INSIDE the window and the rightmost panel's bottom-right corner a half
  slant OUTSIDE it: the window showed the difference as a black wedge down each side. A parallelogram
  cannot express the shape at all, so `psxport::Pane` grew a second down-vector — `uAxisW`, down the RIGHT
  edge — and the pane shader maps the unit square through `origin + u*axisU + v*mix(axisV, axisW, u)`.
  With `axisW == axisV` that is exactly the affine map it always was, so every solid pane (the dividers,
  the caption strip) is unchanged; a trapezoid is the one case the third vector exists for. The crop's
  aspect uses the panel's AVERAGE width, because a trapezoid has two.
- **The caption has a backing strip**, drawn by the compositor as a translucent dark pane over the
  selected panel's own band (alpha 0.62) rather than as Rml CSS: the text sits over a running demo, and
  yellow on a sunlit canyon is unreadable without it.

## Answering the reported defects

- **"The selected panel is black while the others show pictures"** — its session was presenting a frame
  with no picture in it (between the guest's own screens). Fixed by the held frame above.
- **"A stray inset rectangle over the right panel"** — it is Spyro 3's own memory-card prompt, drawn by
  the game inside its own picture. Its windowed shot (`cmp_windowed.ppm`, 512x240) shows the same prompt
  full-screen, so nothing foreign is composited into that pane.
- **"Panels have black bands top and bottom"** — the guest's own border rows (see above), now measured
  and cropped.
- **"Margins show stale teal pictures after confirm"** — not reproduced after the per-Core render-path
  fix: the confirmed shot is the session's own picture (Spyro 2 at 684x240 with its own widescreen
  margins, Spyro 3 at 512x240 native). A confirmed session's picture is its own because its VRAM is the
  same VRAM it used as a panel; the pane never wrote to it.

## Root cause of the multi-session faults (framework, fixed here)

Both faults were ONE defect, and it was process-global state shared across Cores — the render-path CVar.
`render_path_install` wrote a title's fallback into `cv_render_path` at `Layer::Runtime`, a
process-global slot. Spyro 2 declares only the guest paths, so Spyro 2's boot wrote `gte` there, and
**every Core created afterwards resolved its render path from it**: Spyro 1's panel session then booted
on the guest renderer instead of the native one, and the retained-reference leg
(`game/render/render_frame.cpp::referenceOtWalk`) deliberately stops at the first frame-driver call —
`frame-render-drv required a completed guest call, but execution exited as frame-boundary`. The same
mix put a session on a renderer its title does not use, which is how the long-walk confirm reached a
NULL BIOS function pointer.

Fixed in psxport: a live `render path ...` switch records the Core it was addressed to and a Core adopts
one only if the switch is its own (`render_path.cpp`); a refused title path is recorded on the Core and
in the install line, never in the process-global ladder; `render_path_forget(Core*)` from `~Core` keeps
the remembered pointer from outliving its machine; `render_path_excluding_runtime` reads the ladder
without that one layer. Other process-globals fixed for the same reason: the vendored Beetle SPU and XA
bindings are re-bound per stepped frame in `FrameLoopShell::step()`, and the host field clock is per-Core.

- **Panel count is a configuration path, not a filesystem one.** `PSXPORT_PICKER_TITLES=spyro1,spyro3`
  puts a named subset of the catalog on screen, and an empty value (the default) is every provisioned
  title. One, two or three panels are therefore the same code path with different configuration; the
  provisioned disc images are never moved, renamed or hidden to make a capture.

## Not verified / residual

- **Pointer clicks on a panel are not implemented.** The panels replaced the text rows, so the rows'
  click targets are gone and the selector is pad/keyboard/control-channel only. The design named
  Left/Right, D-pad and keyboard, so this is a deliberate consequence rather than a defect; a pointer
  would need the panel's parallelogram hit-tested against the RmlUi pointer position.
- The picker's own Game builds the full 6-tab ESC menu, and so does each of the three panel sessions
  (they never draw it). A panel session does not need a menu: `RmlOverlay::init` has no "menu, please"
  argument, and adding one is a small framework change left undone.
- Windowed behaviour was driven headless only (agents must not open the player's window). The composite
  path is leg-independent by construction, and the window blit is the one step not exercised here.
- RmlUi's debugger plugin prints "already initialised" for the second and third overlay. Harmless
  (diagnostics only), and not yet routed through the same process lifetime as the library itself.
- **A full-width pane showed noise, not a picture — FIXED.** With one title available the pane image was the
  whole 1280-wide surface, and Spyro 1's presented image on that route was deterministic coloured noise.
  The cause was in `PresentPlan`: `content_w` (the guest's own authored display width, 512 for Spyro 1)
  was narrowing the SOURCE rectangle while the present sampled the ires composite — which our own raster
  has already rendered at the DISPLAY's width, 2046 columns. Narrowing there sampled the wrong 1536 of
  2046 columns, and a panel drew that as noise. The narrowing now applies to the native VRAM read only,
  for the same reason 24bpp already did; both pane and window presentation went through the same plan and
  the same fix, so there is no second implementation of the window's decode path. Verified at 1280x720
  with one full-width panel: correct colours, full bleed, slanted edge, caption strip.
- **Spyro 2 and Spyro 3 run again**, so the three-panel capture is unblocked. The two faults recorded
  earlier in this issue (`UNMAPPED RAM write16 @ 0x00816000` in `guest_terrain::drawDetailSectors`, and
  the recompiled-code scratchpad access at `0x1F800442` in block `0x8002d180`) belonged to the concurrent
  refactor that moved both titles' renderers to title-neutral `game/render/guest_*` modules and are not
  reproduced now. Each title reaches its own attract demo in a panel.
- **The panel refresh cost is bounded but not zero.** A shown panel replaces its held frame at most once
  every fifteen of its own frames, each replacement being one GPU->CPU->GPU round trip through the
  framework's present readback. At three panels that is under twenty round trips a second in total, and
  zero while a panel is not being shown. Refreshing every frame instead would be the obvious way to make
  panels perfectly smooth and is exactly the cost the hold exists to avoid.
- A title with no attract demo reachable without input (Spyro 3's "press start" title screen held for
  minutes before its demo took over) shows its own title screen in the panel for as long as it holds it.
  That is the title's behaviour, not the selector's, and it is content rather than a boot card.
- `shared/lightrec` carries two commits behind the pinned `PSXPORT_LIGHTREC_REVISION`; both have been
  pushed by the operator, so a fresh clone builds them.

## 2026-10-02 second pass (composite correctness + logo facts hunted down)

- **Pane texture upload used four times the buffer's row stride.** `PanelLogo::texture`
  set `pixels_per_row = width * 4` where pixels are the RGBA8 unit of the transfer
  buffer; every other repo reader uses text width in pixels. The uploaded texture
  therefore decoded as stripes and never produced a visible wordmark no matter how
  correct the extraction was. Fixed to `pixels_per_row = width`; the decode path
  draws correctly when it produces a non-empty RGBA.
- **Sub-rect panes were clipped by the seams of the row they sat in, not by the
  surface.** The pane shader evaluated the seam at a pane-local `v` (`v_uv.y`). On
  a sub-rect pane (a logo over its panel) that mapped the seam's top/bottom endpoints
  from the surface onto the logo's own local height, cutting it along the wrong row.
  The vertex stage now passes `v_down = p.y / viewport.h` (the fragment's own row as
  a fraction of the surface), which is identical for full-height panes and correct
  for sub-rects.
- **All three title-logo facts were stale publisher-card rectangles.** The card art
  they named is not drawn by any Spyro product in this repo (Spyro 1/2/3 go boot
  straight to their own title screens); the extraction "succeeded" only because the
  2% acceptance threshold accepted GPU-noise. All three `*_logo_facts.h` now declare
  no rectangle (`TitleLogoFacts{}`), so extraction refuses them and the panel draws
  nothing rather than a noise rectangle or a black box. The transparent-logo path is
  preserved and exercised; the missing facts are the remaining RE (tracked under
  `picker.logo` in the RE frontier).
- Leftover scratch-probe scripts (`scratch/*.py`, `scratch/*.out`,
  `scratch/logore2.out`, …) deleted; `tools/picker_look.py` and
  `tools/picker_confirm_probe.py` no longer exist.
