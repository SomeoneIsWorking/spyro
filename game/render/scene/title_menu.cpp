// title_menu.cpp — the stage-13 sprite layer: a port of the guest emitter 0x8007CD38.
//
// 0x8007CD38 builds one POLY_FT4 sized entirely from the sprite table, so no GTE is on this path,
// and a negative id mirrors it. Its packet lands on the front list at [0x8007581C].
#include "core.h"
#include "frame_renderer.h"
#include "game.h"
#include "guest_globals.h"
#include "producer_scope.h" // ProducerScope — the native leg's "who is drawing right now"
#include "render_queue.h"
#include "title_menu_recipe.h"
#include "title_menu_state.h"
#include "ui_anchor.h"
#include <lucent/log.h>

namespace {

// The producer key is the guest submitter this file stands in for: spriteEmit IS 0x8007CD38, and
// keying the shared AddPrim or OT walk instead would name the library rather than the effect.
constexpr uint32_t kGuestSpriteEmitter = 0x8007CD38u;

// The two ease tables the logo animates along, 16 bytes each, indexed by the animation value.
constexpr uint32_t kEaseDropIn = 0x8006FA74u;
constexpr uint32_t kEaseSlideOut = 0x8006FA84u;
constexpr uint32_t kEaseBanner = 0x8006FA64u;
constexpr uint32_t kEaseLen = 16u;   // both drop-in and slide-out are 16 entries
constexpr int32_t kLogoYBias = 0x80; // the arm subtracts this from the table byte
constexpr int32_t kBannerYBias = 0x77;

// The sprite table: 64 records of 8 bytes at 0x8006FACC. Id 64 is already garbage, so the table is
// exactly 64 long and the bound below is the game's.
constexpr uint32_t kSpriteTable = 0x8006FACCu;
constexpr uint32_t kSpriteCount = 64u;
constexpr uint32_t kSpriteStride = 8u;

// The style table: 4 GP0 command+colour words at 0x8006FABC. The code byte is 0x2C on all four —
// textured, modulated, opaque.
constexpr uint32_t kStyleTable = 0x8006FABCu;
constexpr uint32_t kStyleCount = 4u;

// The arm writes `((anim & 0xF) < 8) << 1`: style 2 for half a 16-frame cycle, style 0 for the
// rest.
constexpr uint32_t kStyleBright = 2u;
constexpr uint32_t kStyleNeutral = 0u;
constexpr uint32_t kBlinkMask = 0xFu;
constexpr uint32_t kBlinkHalf = 8u;

constexpr int32_t kIdLogo = 0;             // 255x128 title art, tpage 0x98
constexpr int32_t kIdBanner = 1;           // 148x128 art, drawn once plus once mirrored
constexpr int32_t kIdPressStrip = 11;      // 128x16 strip under the logo, the one that pulses
constexpr int32_t kLogoX = 0x80;           // 128
constexpr int32_t kStripX = 0xC0;          // 192
constexpr int32_t kStripY = 0xD2;          // 210
constexpr int32_t kBannerXLeft = 0x6C;     // 108
constexpr int32_t kBannerXRight = 0xFF;    // 255 — the mirrored copy
constexpr uint32_t kAnimBannerFrom = 0x10; // mode 0 page 4 switches from slide-out to banner here

// Every element is authored in a 384-wide space symmetric about its own middle, so the class is
// centred: the banner is drawn at 108 and mirrored at 255, and the text columns sit at 128 and 256.
constexpr spyro::ui_anchor::Anchor kTitleMenuAnchor = spyro::ui_anchor::Anchor::Centred;

// One sprite table record.
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

// spriteEmit — the port of 0x8007CD38: one screen-space textured quad, straight into the render
// queue, mirrored horizontally when `id < 0`.
//
// The queue takes VRAM-absolute coordinates, so the active draw env's offset is passed in rather
// than re-read per sprite. The result is whether a quad reached the queue.
bool spyro::render::FrameRenderer::spriteEmit(int32_t x,
                                              int32_t y,
                                              int32_t id,
                                              uint32_t style,
                                              int32_t drawOfsX,
                                              int32_t drawOfsY,
                                              int32_t clipX0,
                                              int32_t clipY0,
                                              int32_t clipX1,
                                              int32_t clipY1,
                                              const char *element,
                                              std::size_t index) const {
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

  int ys[4] = {y, y, y + s.h, y + s.h};
  // Placed as a BOX through the one anchoring owner, so the sprite keeps its authored pixel size
  // and only its origin moves. At 4:3 the offset is zero and these are the guest's own coordinates.
  const spyro::ui_anchor::Placed placed = spyro::ui_anchor::placeAndReport(
      {element, index}, kTitleMenuAnchor, x + drawOfsX, s.w, spyro::ui_anchor::frame(c));
  const int placedX = placed.box.x;
  const int xs[4] = {placedX, placedX + placed.box.width, placedX, placedX + placed.box.width};
  for (int i = 0; i < 4; i++) {
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
  // The one push below is the only thing in this scope: a wider scope would put the two early
  // `return false` paths inside a scope that pushes nothing.
  ProducerScope producer(&c->rsub.producerScope, kGuestSpriteEmitter, "titlefx:spriteEmit");
  // The x above is FINAL: `ui_anchor` has already placed this element, so the queue's own centring
  // rule must not be applied on top of it. At 4:3 the two are the same number.
  RenderQueue::Space2dScope anchored(c->game->rq, RQ_2D_WIDE_FINAL);
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

// titleMenuRender — the port of 0x8007CEE4's sprite half, modes 0, 1 and 2. False means the frame's
// mode has no producer, so the seam can abort naming it.
bool spyro::render::FrameRenderer::titleMenuRender(int32_t drawOfsX,
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
                            clipY1,
                            "title-menu-item",
                            i)
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
                  spyro::guest::kTitlescreenState,
                  st.mode);
    return false;
  }

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
                              clipY1,
                              "title-logo",
                              0)
                       ? 1
                       : 0;
      }
    }
  } else if (st.page == 3) {
    arm = Arm::kLogoHold;
    emitted += spriteEmit(kLogoX,
                          0,
                          kIdLogo,
                          kStyleNeutral,
                          drawOfsX,
                          drawOfsY,
                          clipX0,
                          clipY0,
                          clipX1,
                          clipY1,
                          "title-logo",
                          0)
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
                          clipY1,
                          "title-press-strip",
                          0)
                   ? 1
                   : 0;
  } else if (st.page == 4) {
    if (st.anim < kAnimBannerFrom) {
      arm = Arm::kLogoSlideOut;
      const int32_t y = (int32_t)c->mem_r8(kEaseSlideOut + st.anim) - kLogoYBias;
      emitted += spriteEmit(kLogoX,
                            y,
                            kIdLogo,
                            kStyleNeutral,
                            drawOfsX,
                            drawOfsY,
                            clipX0,
                            clipY0,
                            clipX1,
                            clipY1,
                            "title-logo",
                            0)
                     ? 1
                     : 0;
    } else {
      // The same 148-wide sprite drawn once and once MIRRORED. The guest indexes 0x8006FA64 by the
      // raw anim value with no bound; this port declines past the table.
      arm = Arm::kBanner;
      if (st.anim >= kEaseLen * 4u) { // the banner table is 16 4-byte groups
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
                              clipY1,
                              "title-banner",
                              0)
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
                              clipY1,
                              "title-banner",
                              1)
                       ? 1
                       : 0;
      }
    }
  }

  // One line per frame, with its own denominators. `emitted=0` under a named arm is a real answer
  // and must not read the same as "no arm".
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
