// fx_title_menu.cpp — SPYRO'S FIRST NATIVE PRODUCER: the front-end sprite layer of stage 13.
//
// WHY THIS ONE. It is the scene the render seam's abort names FIRST on a real run, and the abort
// order IS the dependency order (docs/re-frontier.md `frame.own-render-driver`). Measured, native
// leg, idle boot: `NATIVE RENDER NOT IMPLEMENTED — stage selector = 13`, arm `SPLIT on
// [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4`, with `[0x80078D78] = 0` — so the handler this
// port has to produce is 0x8007CEE4, in the OVERLAY, and `tools/whatis.py 0x8007CEE4 --ram
// scratch/raw/snap_title_3000.bin` confirms OV_5B800 is the resident image on 256/256 first words.
//
// WHAT IT DRAWS. Spyro's front end is a sequence of full-width sprites over a 3D backdrop: the logo
// dropping in on an ease table, the logo sitting with a pulsing strip under it, the logo sliding
// out, and a mirrored banner pair. Every one of them is ONE call to the guest's sprite emitter
// 0x8007CD38, and that emitter is what this file actually ports.
//
// ────────────────────────────────────────────────────────────────────────────────────────────────
// RE — GROUND TRUTH. Ghidra headless (external/psxport/tools/decomp.sh) over
// scratch/raw/snap_title_3000.bin, a RAM dump taken at the title screen so the resident overlay is
// the one that runs:
//     scratch/decomp/title_stage13.c   FUN_8007cee4 — the stage-13 arm and its state machine
//     scratch/decomp/title_text.c      FUN_8007cd38 — the sprite emitter ported below
//     scratch/decomp/title_addprim.c   FUN_800168dc / FUN_80016784 — where the prim lands in the OT
//
// 1. THE EMITTER 0x8007CD38(x, y, id, style) builds ONE POLY_FT4 (tag 0x09000000 = 9 words) and
//    links it. Its geometry is a screen-space rectangle whose size comes entirely from the sprite
//    table, so there is no projection and no GTE anywhere on this path:
//        v0 = (x,     y    )   uv0 = (U,     V    )
//        v1 = (x + w, y    )   uv1 = (U + w, V    )
//        v2 = (x,     y + h)   uv2 = (U,     V + h)
//        v3 = (x + w, y + h)   uv3 = (U + w, V + h)
//    A NEGATIVE id means "mirror": the emitter swaps the U column pairs AFTER linking the packet
//    (the GPU only reads it later, at DrawOTag), which is how the wide banner is drawn as one
//    148-wide sprite plus its reflection.
//
// 2. WHERE IT LANDS. 0x800168DC pushes the packet onto the FRONT list at [0x8007581C], and
//    0x80016784 (the OT walk the frame's DrawOTag consumes) links that list at the very front — so
//    these sprites are the TOPMOST layer of the frame. Hence RQ_HUD, not RQ_BACKGROUND.
//
// 3. THE TABLES ARE IN MAIN, NOT THE OVERLAY (0x8006FABC / 0x8006FACC are below the overlay arena
//    at 0x8007AA38), so they do not move when the overlay changes. Both were dumped from the
//    snapshot and read as clean, self-consistent records for exactly ids 0..63 — see the layout
//    comment on SpriteRec below.
//
// NOT A PICTURE READ BACK OUT OF THE GPU. Every input is the game's own state: four menu-state
// globals, an animation index, two ease tables and the sprite table. No GTE register is read, no OT
// or GP0 packet is inspected, and no gen_func_* body runs to make this picture.
//
// WHAT IS *NOT* PORTED, stated here rather than discovered later:
//   * Mode 2 of 0x8007CEE4 (the 3-slot save screen). Mode 1's memory-card pages are transcribed
//     through title_menu_recipe.cpp from the same overlay body and corroborated by the vendored
//     TitlescreenDraw Rosetta source. Its hermetic recipe coverage is current, but its real-data
//     command differential remains serialized run work (issue 0085).
//   * The 3D backdrop of stage 13 — 0x800521C0, 0x8001F158, 0x8001F798, 0x800258F0, 0x8004EBA8.
//     Those are the world renderer, shared with the FIELD arm, and they are the next producers.
//     Reported every frame on the `titlefx` channel with their own denominator.
//
// DIAGNOSTIC. `PSXPORT_DEBUG=titlefx` prints one line per producer call carrying the state that
// selected the arm, the arm taken, how many sprites were emitted, and the world layers still
// missing. `emitted=0` with a named arm is a real answer (the d88==2 arm draws nothing until the
// game's own gate opens) and reads differently from `arm=none`.
//
// ────────────────────────────────────────────────────────────────────────────────────────────────
// PRODUCER DB — THIS FILE IS THE PORT'S *ONLY* NATIVE PRODUCER, AND ITS KEY IS 0x8007CD38 (#59).
//
// The native leg's census has no trail of its own: `push2dQuad` funnels into the one render-queue
// chokepoint (psxport render_queue.cpp emitOrQueue), and a prim arriving there is anonymous unless
// the producer SAYS who it is. Until this scope existed, `grep -rn ProducerScope game/` returned 0
// across this whole tree, so the DB could only ever report "NEVER FED" — which is not "this game
// draws nothing", it is "no instrument exists here". Issue #59 is exactly that gap.
//
// WHY 0x8007CD38 AND NOT ONE OF THE THREE OTHER CANDIDATES ON THIS PATH (producer_scope.h's rule:
// key on the GUEST SUBMITTER the native draw stands in for, never a shared dispatcher, never a
// shared submit leaf). All four are real addresses in this file's own RE, so the choice is between
// four true statements, only one of which is the right row:
//   * 0x8007CD38  ← CHOSEN. `spriteEmit` below IS the transcription of it, one native quad per
//   guest
//     POLY_FT4, same inputs, same table reads. It is the finest-grained guest fn that SUBMITS, so
//     the guest leg (OtAttr, keyed at the packet-pool store) and this native scope land on the SAME
//     row — which is the entire point of the DB.
//   * 0x8007CEE4 — the stage-13 arm/state machine. It selects a page and calls the emitter; it
//     submits nothing itself. Keying here would be keying a DISPATCHER, and because the census
//     charges a prim to the INNERMOST open scope only, an outer 0x8007CEE4 scope would also mint a
//     permanently 0-prim row that reads as "a producer that draws nothing".
//   * 0x800168DC / 0x80016784 — the front-list AddPrim and the OT walk. These are the SHARED SUBMIT
//     LEAF the whole game funnels through: keying them would name the library instead of the effect
//     and collapse every future producer into one meaningless row.
//
// NO DisplayPassGuard HERE, and that is a decision rather than an omission. Tomba!2's worked
// example (game/render/perobj_dispatch.cpp:257) needs one because its native draw lives INSIDE a
// substrate body whose surrounding guest code legitimately writes guest RAM, so the pc_render
// read-only-overlay invariant has to be armed for just that block. This producer is not inside any
// `gen_func_*` body: its whole call chain is port code (Spyro1FrameDriver ->
// SpyroRenderer::drawFrame -> renderScene -> titleMenuRender -> here), it only READS guest memory,
// the ease tables, the sprite/style tables), and it writes none. Arming the guard would therefore
// assert nothing this call chain can violate — and it would ALSO fire on the guest calls the
// enclosing frame legitimately makes, since the guard is per-Core and this frame's env setup
// (frame_env.cpp) does write guest RAM.
#include "core.h"
#include "game.h"
#include "producer_scope.h" // ProducerScope — the native leg's "who is drawing right now"
#include "render.h"
#include "render_queue.h"
#include "title_menu_recipe.h"
#include "title_menu_state.h"
#include <lucent/log.h>

namespace {

// ── PRODUCER IDENTITY: the guest submitter this file stands in for ───────────────────────────────
// The sprite emitter RE'd at the top of this file (`scratch/decomp/title_text.c` FUN_8007cd38).
// `spriteEmit` below is its transcription, so its native prims belong in ITS row — see the
// PRODUCER DB block in the banner for why not 0x8007CEE4, 0x800168DC or 0x80016784.
constexpr uint32_t kGuestSpriteEmitter = 0x8007CD38u;

// ── The two ease tables the logo animates along (main-executable data, 16 bytes each) ────────────
// Byte tables, indexed by kAnim, biased by the emitter's own constant. Dumped from the snapshot:
//   drop-in  0x8006FA74: 0,8,16,25,34,44,54,65,76,88,101,114,128,123,120,126  (ease-out +
//   overshoot) slide-out 0x8006FA84: 127,125,122,118,113,107,100,92,83,73,62,50,37,23,9,0 banner
//   0x8006FA64: 60,2,1,128, 40,2,1,128, 20,2,1,128, 8,2,1,128
constexpr uint32_t kEaseDropIn = 0x8006FA74u;
constexpr uint32_t kEaseSlideOut = 0x8006FA84u;
constexpr uint32_t kEaseBanner = 0x8006FA64u;
constexpr uint32_t kEaseLen = 16u;   // both drop-in and slide-out are 16 entries
constexpr int32_t kLogoYBias = 0x80; // the arm subtracts this from the table byte
constexpr int32_t kBannerYBias = 0x77;

// ── The sprite table: 64 records of 8 bytes at 0x8006FACC ────────────────────────────────────────
// Layout read off 0x8007CD38's own loads (`&DAT_8006facc + id*8` for the two u16s, `&DAT_8006fad0 +
// id*8` for the four u8s) and confirmed by dumping all 64: ids 0..63 are coherent (three 255x15
// text pages at tpage 0x1C/1D/1E, ten 16x16 glyphs at 0x1A, the 255x128 logo at 0x98), and id 64 is
// already garbage — so the table is exactly 64 long and this bound is the game's, not a guess.
constexpr uint32_t kSpriteTable = 0x8006FACCu;
constexpr uint32_t kSpriteCount = 64u;
constexpr uint32_t kSpriteStride = 8u;

// ── The style table: 4 GP0 command+colour words at 0x8006FABC
// ───────────────────────────────────── Dumped: 2C808080 (neutral), 2C00F0F0 (yellow), 2CC0C0C0
// (bright), 2C404040 (dim). The code byte is 0x2C on all four — POLY_FT4, textured, MODULATED (bit
// 0 clear = not raw), opaque (bit 1 clear).
constexpr uint32_t kStyleTable = 0x8006FABCu;
constexpr uint32_t kStyleCount = 4u;

// The style the pulsing strip alternates between, and the phase that selects it: the arm writes
// `((anim & 0xF) < 8) << 1`, i.e. style 2 for half of a 16-frame cycle and style 0 for the other.
constexpr uint32_t kStyleBright = 2u;
constexpr uint32_t kStyleNeutral = 0u;
constexpr uint32_t kBlinkMask = 0xFu;
constexpr uint32_t kBlinkHalf = 8u;

// ── The sprite ids and screen positions this arm uses, named at their one use site ───────────────
constexpr int32_t kIdLogo = 0;             // 255x128 title art, tpage 0x98
constexpr int32_t kIdBanner = 1;           // 148x128 art, drawn once plus once mirrored
constexpr int32_t kIdPressStrip = 11;      // 128x16 strip under the logo, the one that pulses
constexpr int32_t kLogoX = 0x80;           // 128
constexpr int32_t kStripX = 0xC0;          // 192
constexpr int32_t kStripY = 0xD2;          // 210
constexpr int32_t kBannerXLeft = 0x6C;     // 108
constexpr int32_t kBannerXRight = 0xFF;    // 255 — the mirrored copy
constexpr uint32_t kAnimBannerFrom = 0x10; // mode 0 page 4 switches from slide-out to banner here

// ── A typed lens over one sprite record ──────────────────────────────────────────────────────────
struct SpriteRec {
  uint32_t tpage, clut;
  int32_t w, h, u, v;
  static SpriteRec read(Core *c, uint32_t id) {
    const uint32_t b = kSpriteTable + id * kSpriteStride;
    return {c->mem_r16(b + 0),
            c->mem_r16(b + 2),
            c->mem_r8(b + 4),
            c->mem_r8(b + 5),
            c->mem_r8(b + 6),
            c->mem_r8(b + 7)};
  }
};

// The arms of mode 0, as the guest's if-chain orders them. `kNone` is a real answer: the chain
// draws no sprites for a page it does not name, and the frame is then the 3D backdrop alone.
enum class Arm { kNone, kLogoDropIn, kLogoHold, kLogoSlideOut, kBanner };
const char *armName(Arm a) {
  switch (a) {
  case Arm::kLogoDropIn:
    return "page 2: logo DROPPING IN along the ease table at 0x8006FA74";
  case Arm::kLogoHold:
    return "page 3: logo held + the pulsing strip under it";
  case Arm::kLogoSlideOut:
    return "page 4: logo SLIDING OUT along the ease table at 0x8006FA84";
  case Arm::kBanner:
    return "page 4: the mirrored banner pair";
  case Arm::kNone:
    break;
  }
  return "none — this page draws no sprites (the guest's if-chain names no arm for it)";
}

} // namespace

// spriteEmit — the port of 0x8007CD38. One screen-space textured quad, straight into the render
// queue. `id < 0` mirrors it horizontally, exactly as the guest's post-link U swap does.
//
// `drawOfsX/Y` is the active draw env's offset: the render queue takes VRAM-ABSOLUTE coordinates
// (the reference leg's OT walk adds the same offset before it reaches the queue), and on the native
// leg nothing else applies it. Passing it in rather than re-reading the env per sprite keeps the
// whole frame on ONE env, which is the property the double buffer depends on.
// It returns whether a quad actually reached the render queue, and the arm below counts THAT rather
// than counting its own intentions — so the `emitted=` in the diagnostic can never report sprites
// that were rejected on the way out.
bool SpyroRenderer::spriteEmit(int32_t x,
                               int32_t y,
                               int32_t id,
                               uint32_t style,
                               int32_t drawOfsX,
                               int32_t drawOfsY,
                               int32_t clipX0,
                               int32_t clipY0,
                               int32_t clipX1,
                               int32_t clipY1) const {
  Core *c = mC;
  const bool mirror = id < 0;
  const uint32_t sid = (uint32_t)(mirror ? -id : id);
  if (sid >= kSpriteCount || style >= kStyleCount) {
    // The guest bound-checks neither, so an out-of-range value here means this port's state reading
    // is wrong — not that the game would have drawn garbage. Say so instead of drawing anything.
    lucent::warn("titlefx",
                 "sprite id {} / style {} is outside the tables (ids 0..{}, styles 0..{}) "
                 "— NOTHING emitted for this call",
                 id,
                 style,
                 kSpriteCount - 1,
                 kStyleCount - 1);
    return false;
  }
  const SpriteRec s = SpriteRec::read(c, sid);
  const uint32_t styleWord = c->mem_r32(kStyleTable + style * 4u);
  const unsigned char r = (unsigned char)(styleWord & 0xFFu);
  const unsigned char g = (unsigned char)((styleWord >> 8) & 0xFFu);
  const unsigned char b = (unsigned char)((styleWord >> 16) & 0xFFu);
  const uint32_t code = (styleWord >> 24) & 0xFFu;

  int xs[4] = {x, x + s.w, x, x + s.w};
  int ys[4] = {y, y, y + s.h, y + s.h};
  for (int i = 0; i < 4; i++) {
    xs[i] += drawOfsX;
    ys[i] += drawOfsY;
  }
  // The mirror swaps the two U columns; V is untouched, which is why it is a horizontal flip.
  const int uL = s.u, uR = s.u + s.w;
  const int u0 = mirror ? uR : uL, u1 = mirror ? uL : uR;
  int us[4] = {u0, u1, u0, u1};
  int vs[4] = {s.v, s.v, s.v + s.h, s.v + s.h};
  const unsigned char rs[4] = {r, r, r, r}, gs[4] = {g, g, g, g}, bs[4] = {b, b, b, b};

  // GP0 code bits, from the style word rather than hardcoded: bit 0 = raw texel, bit 1 = semi.
  const int raw = (code & 1u) ? 1 : 0;
  const int semi = (code & 2u) ? 1 : 0;
  // DECLARE THE PRODUCER for the one push below, and no wider. Scoping the whole function instead
  // would be harmless today (host words only, no guest write) but it would put the two early
  // `return false` paths inside a scope that pushes nothing, which is precisely the shape that
  // makes a 0-prim row indistinguishable from a producer that never ran. The name is a string
  // literal, as ProducerScope requires — it is stored by pointer, not copied.
  ProducerScope producer(&c->rsub.producerScope, kGuestSpriteEmitter, "titlefx:spriteEmit");
  c->game->rq.push2dQuad(RQ_HUD,
                         /*order_2d_fg=*/1,
                         xs,
                         ys,
                         us,
                         vs,
                         rs,
                         gs,
                         bs,
                         /*tp_x=*/(int)(s.tpage & 0xFu) * 64,
                         /*tp_y=*/(int)((s.tpage >> 4) & 1u) * 256,
                         /*mode=*/(int)((s.tpage >> 7) & 3u),
                         raw,
                         /*clut_x=*/(int)(s.clut & 0x3Fu) * 16,
                         /*clut_y=*/(int)((s.clut >> 6) & 0x1FFu),
                         /*tw_mx=*/0,
                         /*tw_my=*/0,
                         /*tw_ox=*/0,
                         /*tw_oy=*/0,
                         clipX0,
                         clipY0,
                         clipX1,
                         clipY1,
                         semi);
  return true;
}

// titleMenuRender — the port of 0x8007CEE4's SPRITE half, modes 0, 1, and 2.
//
// Returns false when the frame's mode has no producer, so the seam can abort naming it instead of
// presenting a screen with its menu missing.
bool SpyroRenderer::titleMenuRender(int32_t drawOfsX,
                                    int32_t drawOfsY,
                                    int32_t clipX0,
                                    int32_t clipY0,
                                    int32_t clipX1,
                                    int32_t clipY1) const {
  Core *c = mC;
  const auto st = spyro::title_menu_state::read(c);
  if (st.mode == 1 || st.mode == 2) {
    const auto recipe = st.mode == 1 ? spyro::title_menu_recipe::buildMode1(st.mode1Input())
                                     : spyro::title_menu_recipe::buildMode2(st.mode2Input());
    int emitted = 0;
    for (size_t i = 0; i < recipe.size; ++i) {
      const auto &command = recipe.commands[i];
      emitted += spriteEmit(command.x,
                            command.y,
                            command.sprite,
                            command.style,
                            drawOfsX,
                            drawOfsY,
                            clipX0,
                            clipY0,
                            clipX1,
                            clipY1)
                     ? 1
                     : 0;
    }
    lucent::debug("titlefx",
                  "mode={} substate={} anim={} option={} card={} recipe={} emitted={}",
                  st.mode,
                  st.mode == 1 ? st.page : st.mode2State,
                  st.anim,
                  st.optionSelected,
                  st.cardSelected,
                  recipe.size,
                  emitted);
    return true;
  }
  if (st.mode != 0) {
    lucent::error("render",
                  "  stage 13 mode [0x{:08X}] = {} has NO native producer. Modes 0, 1, and 2 "
                  "(the logo front end, memory-card menus, and 3-slot save screen) are ported.",
                  spyro::title_menu_state::kModeAddress,
                  st.mode);
    return false;
  }

  // ── which arm, and what it emits
  // ────────────────────────────────────────────────────────────────
  Arm arm = Arm::kNone;
  int emitted = 0;
  if (st.page == 2) {
    // The logo drops in — but only once the game's own counter passes its gate.
    arm = Arm::kLogoDropIn;
    if (st.gateOpen) {
      if (st.anim >= kEaseLen) {
        lucent::warn("titlefx",
                     "drop-in anim index {} is past the {}-entry ease table at 0x{:08X}; "
                     "the guest reads it unchecked, so this port declines rather than "
                     "reproduce an out-of-table read. NOTHING emitted.",
                     st.anim,
                     kEaseLen,
                     kEaseDropIn);
      } else {
        const int32_t y = (int32_t)c->mem_r8(kEaseDropIn + st.anim) - kLogoYBias;
        emitted += spriteEmit(kLogoX,
                              y,
                              kIdLogo,
                              kStyleNeutral,
                              drawOfsX,
                              drawOfsY,
                              clipX0,
                              clipY0,
                              clipX1,
                              clipY1)
                       ? 1
                       : 0;
      }
    }
  } else if (st.page == 3) {
    arm = Arm::kLogoHold;
    emitted +=
        spriteEmit(
            kLogoX, 0, kIdLogo, kStyleNeutral, drawOfsX, drawOfsY, clipX0, clipY0, clipX1, clipY1)
            ? 1
            : 0;
    const uint32_t stripStyle =
        ((st.anim & kBlinkMask) < kBlinkHalf) ? kStyleBright : kStyleNeutral;
    emitted += spriteEmit(kStripX,
                          kStripY,
                          kIdPressStrip,
                          stripStyle,
                          drawOfsX,
                          drawOfsY,
                          clipX0,
                          clipY0,
                          clipX1,
                          clipY1)
                   ? 1
                   : 0;
  } else if (st.page == 4) {
    if (st.anim < kAnimBannerFrom) {
      arm = Arm::kLogoSlideOut;
      const int32_t y = (int32_t)c->mem_r8(kEaseSlideOut + st.anim) - kLogoYBias;
      emitted +=
          spriteEmit(
              kLogoX, y, kIdLogo, kStyleNeutral, drawOfsX, drawOfsY, clipX0, clipY0, clipX1, clipY1)
              ? 1
              : 0;
    } else {
      // The banner: the same 148-wide sprite drawn once and once MIRRORED, meeting in the middle.
      // The guest indexes 0x8006FA64 by the raw anim value with no bound of its own; this port
      // declines past the table rather than reading whatever follows it.
      arm = Arm::kBanner;
      if (st.anim >= kEaseLen * 4u) { // the banner table is 16 4-byte groups; see the dump above
        lucent::warn("titlefx",
                     "banner anim index {} is past the table at 0x{:08X} — NOTHING emitted.",
                     st.anim,
                     kEaseBanner);
      } else {
        const int32_t y = (int32_t)c->mem_r8(kEaseBanner + st.anim) - kBannerYBias;
        emitted += spriteEmit(kBannerXLeft,
                              y,
                              kIdBanner,
                              kStyleNeutral,
                              drawOfsX,
                              drawOfsY,
                              clipX0,
                              clipY0,
                              clipX1,
                              clipY1)
                       ? 1
                       : 0;
        emitted += spriteEmit(kBannerXRight,
                              y,
                              -kIdBanner,
                              kStyleNeutral,
                              drawOfsX,
                              drawOfsY,
                              clipX0,
                              clipY0,
                              clipX1,
                              clipY1)
                       ? 1
                       : 0;
      }
    }
  }

  // ONE LINE PER FRAME, carrying its own denominators: the state that chose the arm, the arm, and
  // how many sprites went out. `emitted=0` under a named arm is a real answer (the drop-in arm
  // draws nothing while the gate is shut) and must not read the same as "no arm".
  lucent::debug("titlefx",
                "mode={} page={} anim={} gate={} arm=[{}] emitted={}",
                st.mode,
                st.page,
                st.anim,
                st.gateOpen ? "open" : "shut",
                armName(arm),
                emitted);
  return true;
}
