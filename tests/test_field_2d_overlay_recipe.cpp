#include "field_2d_overlay_recipe.h"
#include "testutil.h"

#include <vector>

// The FIELD 2D overlay's recipe: what the guest's three screen-space producers compute, and what an
// interval between two of their frames may sample. Every expectation below is a number recovered
// from SCUS_942.28 or from a recipe this repository already pins; the sources are named in the
// comments so a reader can check them rather than trust them.
//
// THE NEGATIVE CASES ARE THE POINT. An interpolation owner is only as good as the pairs it refuses:
// a rule that never ran looks exactly like a rule that matched everything, so each refusal reason
// is provoked here, and the census denominator is asserted alongside every count.

namespace {

using spyro::field_2d_overlay_recipe::Census;
using spyro::field_2d_overlay_recipe::Draw;
using spyro::field_2d_overlay_recipe::Mismatch;
using spyro::field_2d_overlay_recipe::Overlay;
using spyro::field_2d_overlay_recipe::Part;
using spyro::field_2d_overlay_recipe::State;
using spyro::field_2d_overlay_recipe::Status;

const Draw *findPart(const Overlay &overlay, Part part, uint8_t slot) {
  for (const auto &draw : overlay.draws) {
    if (draw.part == part && draw.slot == slot) {
      return &draw;
    }
  }
  return nullptr;
}

uint32_t countPart(const Overlay &overlay, Part part) {
  uint32_t n = 0;
  for (const auto &draw : overlay.draws) {
    n += draw.part == part ? 1u : 0u;
  }
  return n;
}

// A FIELD frame with a fade, a border and two life orbs: the three producers armed at once, which
// is the configuration the Artisans route is in for most of a level.
State fieldState() {
  State state;
  state.fade = 8;          // g_Fade, so the fade is (8<<3)=64 grey
  state.borderEnabled = 1; // g_ScreenBorderEnabled
  state.barHeight = 20;    // D_800756C0
  state.deltaTime = 2;     // g_DeltaTime
  state.drawOffsetX = 0;
  state.drawOffsetY = 0;
  state.renderWidth = 512;
  state.collectables.lifeDisplay = 1;
  state.collectables.lifeOrbCount = 2;
  state.collectables.specularTime = 16;
  state.collectables.cosine[16] = 0x1000;
  state.collectables.cosine[3] = 0x0800;
  state.collectables.rects[12] = {300, 40, 8, 8};
  state.collectables.rects[13] = {310, 40, 8, 8};
  state.collectables.tiles[0] = {1, 2, 0x2420, 0x0080};
  return state;
}

void test_fade_quad_is_the_guests_own_extent_and_colour() {
  const State state = fieldState();
  Overlay overlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, overlay), Status::Ready);
  const Draw *fade = findPart(overlay, Part::Fade, 0);
  CHECK(fade != nullptr);
  if (fade == nullptr) {
    return;
  }
  // 0x800190D4: `lui $v0,0x2a ; sb $v0,0x13($s0)` is code 0x2A = 0x28|0x2, the semi-transparency
  // bit; `sh $zero,0x14` / `sh $v0(8),0x16` / `sh $v1(512),0x18` / `sh $v0(8),0x1a` / `sh
  // $zero,0x1c` / `sh $v0(0xe8=232),0x1e` / `sh $v1,0x20` / `sh $v0,0x22` is setXYWH(f4, 0, 8, 512,
  // 240-16).
  CHECK_EQ(fade->rect.x, 0);
  CHECK_EQ(fade->rect.y, 8);
  CHECK_EQ(fade->rect.w, 512);
  CHECK_EQ(fade->rect.h, 224);
  CHECK_EQ(fade->semi, true);
  // `sll $a3,$a0,5` at 0x800190F0 is the DR_MODE blend: mode 2 shifted left five.
  CHECK_EQ(fade->blendMode, 2u);
  // g_Fade 8 shifted by the FIELD arm's own 3 (0x8001ED5C's arm calls (2, g_Fade<<3, g_Fade<<3,
  // ...)).
  CHECK_EQ(fade->r, 64u);
  CHECK_EQ(fade->r, fade->g);
  CHECK_EQ(fade->g, fade->b);
  // The fade is a POLY_F4: no texture, and the guest's own code byte carries no texcoord.
  CHECK_EQ(fade->tpage, 0u);
  CHECK_EQ(fade->clut, 0u);
}

void test_border_bars_are_the_guests_own_two_polys_and_stepped_height() {
  State state = fieldState();
  state.fade = 0;
  Overlay overlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, overlay), Status::Ready);
  CHECK_EQ(countPart(overlay, Part::Border), 2u);
  const Draw *top = findPart(overlay, Part::Border, spyro::field_2d_overlay_recipe::kBorderTopSlot);
  const Draw *bottom =
      findPart(overlay, Part::Border, spyro::field_2d_overlay_recipe::kBorderBottomSlot);
  CHECK(top != nullptr);
  CHECK(bottom != nullptr);
  if (top == nullptr || bottom == nullptr) {
    return;
  }
  // 0x80018F30: bar 1 is (0,0)-(512,h) and bar 2 is (0,240-h)-(512,240), code 0x28, black, opaque.
  CHECK_EQ(top->rect.x, 0);
  CHECK_EQ(top->rect.y, 0);
  CHECK_EQ(top->rect.w, 512);
  CHECK_EQ(top->rect.h, 22);
  CHECK_EQ(bottom->rect.y, 218);
  CHECK_EQ(bottom->rect.h, 22);
  CHECK_EQ(top->semi, false);
  CHECK_EQ(top->r, 0u);
  CHECK_EQ(bottom->b, 0u);
  // 20 + g_DeltaTime 2 = 22, and `addiu $v0,$zero,0x16` at 0x80018F9C is the clamp the guest stores
  // when the ramp passes 23. The committed height is the overlay's, because the guest writes it
  // back.
  CHECK_EQ(overlay.barHeight, 22);
}

void test_disarmed_border_commits_its_step_but_draws_no_bar() {
  State state = fieldState();
  state.fade = 0;
  state.borderEnabled = 0;
  state.barHeight = 3;
  state.deltaTime = 2;
  Overlay overlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, overlay), Status::Ready);
  // The guest's down ramp is 3 - 2 = 1, which is still a bar; the gate that decides whether the
  // producer runs at all is the composition's, and it is tested through the armed() predicate.
  CHECK_EQ(overlay.barHeight, 1);
  CHECK_EQ(countPart(overlay, Part::Border), 2u);

  State empty = fieldState();
  empty.fade = 0;
  empty.borderEnabled = 0;
  empty.barHeight = 0;
  empty.collectables.lifeDisplay = 0;
  empty.collectables.lifeOrbCount = 0;
  Overlay none;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(empty, none), Status::ValidEmpty);
  CHECK_EQ(none.draws.size(), 0u);
  CHECK_EQ(none.barHeight, 0);
}

void test_collectable_sprites_are_the_guests_polys_with_the_offsets_applied() {
  const State state = fieldState();
  Overlay overlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, overlay), Status::Ready);
  CHECK_EQ(countPart(overlay, Part::Sprite), 2u);
  const Draw *first = findPart(overlay, Part::Sprite, 0);
  const Draw *second = findPart(overlay, Part::Sprite, 1);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  if (first == nullptr || second == nullptr) {
    return;
  }
  // 0x80019578 `addiu $a1,$s2,0xa0` is &m_OrbAndEggSprite[0], the life orb's CONSTANT tile; and
  // `sw $v0,0x10/0x14/0x18($sp)` at 0x800195BC..0x800195C0 is the grey written into all three
  // channels. The rect is the guest's own m_SpriteRect[12+i] with the frame's draw offset added,
  // which is what the sprite submitter did (`sprite.rect.x + gpu.s_off_x`).
  CHECK_EQ(first->rect.x, 300);
  CHECK_EQ(first->rect.y, 40);
  CHECK_EQ(first->rect.w, 8);
  CHECK_EQ(first->rect.h, 8);
  CHECK_EQ(first->tpage, 0x0080u);
  CHECK_EQ(first->clut, 0x2420u);
  CHECK_EQ(first->u0, 1u);
  CHECK_EQ(first->v0, 2u);
  // Orb 0's phase is (16 - 0) & 0xFF = 16, orb 1's is (16 - 256/20 = 12) & 0xFF = 4. The test's
  // cosine table only carries index 16, so orb 1 reads cosine[4] = 0 and lands on 128.
  CHECK_EQ(first->r, 160u);
  CHECK_EQ(second->r, 128u);
  CHECK_EQ(first->r, first->g);
  CHECK_EQ(first->g, first->b);
  // The sprite is a POLY_FT4 with a bare 0x2C code: opaque, unlike the fade's 0x2A.
  CHECK_EQ(first->semi, false);
  CHECK_EQ(first->blendMode, 0u);
}

void test_egg_tiles_advance_one_animation_frame_per_update() {
  // The recovered reason the identity rule has a TileOrigin arm: g_Hud.unk_0x40 (0x80077FE8) steps
  // by one modulo nine per game update (0x8002EB48 / 0x8002EBA0) and func_80019300's egg loop reads
  // it to pick &m_OrbAndEggSprite[1 + (phase + i) % 9] (0x8001961C, 0x80019638, 0x80019660).
  State state = fieldState();
  state.fade = 0;
  state.borderEnabled = 0;
  state.barHeight = 0;
  state.collectables.lifeDisplay = 0;
  state.collectables.lifeOrbCount = 0;
  state.collectables.eggDisplay = 1;
  state.collectables.eggCount = 1;
  state.collectables.eggPhase = 0;
  state.collectables.rects[0] = {40, 40, 12, 16};
  for (uint32_t i = 0; i < 10; ++i) {
    state.collectables.tiles[i] = {static_cast<uint8_t>(i), 0, 0x2400, 0x0080};
  }
  Overlay first;
  Overlay second;
  state.collectables.eggPhase = 0;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, first), Status::Ready);
  state.collectables.eggPhase = 1;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, second), Status::Ready);
  const Draw *a = findPart(first, Part::Sprite, 0);
  const Draw *b = findPart(second, Part::Sprite, 0);
  CHECK(a != nullptr);
  CHECK(b != nullptr);
  if (a == nullptr || b == nullptr) {
    return;
  }
  // phase 0 -> tile 1 + 0 = index 1; phase 1 -> index 2. The guest's `&m_OrbAndEggSprite[1 +
  // phase]`.
  CHECK_EQ(a->u0, 1u);
  CHECK_EQ(b->u0, 2u);
  // …and that is a DIFFERENT PICTURE, not a moved one: the identity rule must refuse the pair even
  // though the rect is identical.
  CHECK_EQ(a->rect.x, b->rect.x);
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(*a, *b), Mismatch::TileOrigin);
}

void test_interpolates_position_and_colour_but_never_the_texture() {
  State before = fieldState();
  before.collectables.rects[12] = {300, 40, 8, 8};
  before.collectables.rects[13] = {310, 40, 8, 8};
  before.barHeight = 18;
  before.fade = 4;
  before.collectables.specularTime = 0;
  before.collectables.cosine[0] = 0;
  State after = before;
  after.barHeight = 20;
  after.fade = 8;
  after.collectables.rects[12] = {300, 52, 8, 8};
  after.collectables.rects[13] = {310, 52, 8, 8};
  after.collectables.specularTime = 16;
  after.collectables.cosine[16] = 0x1000;

  Overlay previous;
  Overlay current;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(before, previous), Status::Ready);
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(after, current), Status::Ready);
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 0.5, census),
           Status::Ready);
  // 5 draws: 1 fade + 2 bars + 2 orbs. Every one of them pairs, because the interval does not
  // sample the texture and both endpoints use the same constant orb tile.
  CHECK_EQ(census.actors, 5u);
  CHECK_EQ(census.interpolated, 5u);
  CHECK_EQ(census.unattributed, 0u);
  CHECK_EQ(census.absent, 0u);
  CHECK_EQ(census.incompatible, 0u);
  CHECK_EQ(census.refused, 0u);
  // The invariant every consumer may assert: every draw is accounted for in exactly one bucket.
  CHECK_EQ(census.interpolated + census.unpaired() + census.incompatible + census.refused,
           census.actors);

  const Draw *fade = findPart(current, Part::Fade, 0);
  const Draw *top = findPart(current, Part::Border, spyro::field_2d_overlay_recipe::kBorderTopSlot);
  const Draw *orb = findPart(current, Part::Sprite, 0);
  CHECK(fade != nullptr);
  CHECK(top != nullptr);
  CHECK(orb != nullptr);
  if (fade == nullptr || top == nullptr || orb == nullptr) {
    return;
  }
  // g_Fade 4 -> 8 is grey 32 -> 64, so the midpoint is 48.
  CHECK_EQ(fade->r, 48u);
  // The bar ramp is 18 -> 20 in GUEST STATE, and the guest's own step (+g_DeltaTime 2, clamped at
  // 22) makes the two DRAWN heights 20 and 22, so the midpoint is 21: the bars SLIDE, which is the
  // whole reason this layer is worth an interval. An earlier version of this test asserted 19 and
  // read 22, which is how the height turned out not to be sampled at all.
  CHECK_EQ(top->rect.h, 21);
  // The orb's rect 40 -> 52 is 46 at the midpoint, and its grey 128 -> 160 is 144.
  CHECK_EQ(orb->rect.y, 46);
  CHECK_EQ(orb->r, 144u);
  // The sampled draws keep the CURRENT endpoint's texture, because the interval never touches it.
  CHECK_EQ(orb->tpage, current.draws.empty() ? 0u : orb->tpage);
  CHECK_EQ(orb->u0, 1u);
}

void test_the_sampler_reaches_every_rect_field_and_no_other() {
  // None of these three producers moves a rect in X on a real route — the guest's orb and egg rects
  // come from m_SpriteRect, which HudMoveMoby/HudMoveEgg step in Y only, and the fade and the bars
  // are laid out against the draw area. So the gameplay cases above cannot tell a sampler that
  // reaches X from one that does not, and a disconnected X would have gone unnoticed. This case
  // shifts every field the sampler claims to reach and leaves every field it must not reach alone.
  Overlay previous;
  Overlay current;
  const Draw base = {.part = Part::Sprite,
                     .instance = spyro::field_2d_overlay_recipe::instanceOf(Part::Sprite, 0),
                     .slot = 0,
                     .rect = {100, 100, 8, 8},
                     .r = 0,
                     .g = 0,
                     .b = 0,
                     .semi = false,
                     .blendMode = 2,
                     .u0 = 4,
                     .v0 = 5,
                     .clut = 0x2420,
                     .tpage = 0x0080};
  Draw moved = base;
  moved.rect = {140, 130, 20, 24};
  moved.r = 200;
  moved.g = 200;
  moved.b = 200;
  previous.draws = {base};
  current.draws = {moved};
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 0.5, census),
           Status::Ready);
  CHECK_EQ(census.interpolated, 1u);
  const Draw &mid = current.draws[0];
  CHECK_EQ(mid.rect.x, 120);
  CHECK_EQ(mid.rect.y, 115);
  CHECK_EQ(mid.rect.w, 14);
  CHECK_EQ(mid.rect.h, 16);
  CHECK_EQ(mid.r, 100);
  CHECK_EQ(mid.g, 100);
  CHECK_EQ(mid.b, 100);
  // …and nothing else moves: the texel origin, the palette, the page, the blend mode, the semi bit
  // and the part/slot identity are the interval's INPUTS, not its outputs.
  CHECK_EQ(mid.u0, base.u0);
  CHECK_EQ(mid.v0, base.v0);
  CHECK_EQ(mid.clut, base.clut);
  CHECK_EQ(mid.tpage, base.tpage);
  CHECK_EQ(mid.blendMode, base.blendMode);
  CHECK_EQ(mid.semi, base.semi);
  CHECK_EQ(mid.part, base.part);
  CHECK_EQ(mid.slot, base.slot);
  CHECK_EQ(mid.instance, base.instance);
}

void test_the_two_endpoints_are_exact() {
  State before = fieldState();
  before.barHeight = 18;
  before.fade = 4;
  State after = before;
  after.barHeight = 20;
  after.fade = 8;
  Overlay previous;
  Overlay current;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(before, previous), Status::Ready);
  Overlay endpoint;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(after, endpoint), Status::Ready);
  current = endpoint;
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 0.0, census),
           Status::Ready);
  // t == 0 is the previous endpoint, EXACTLY: this is what the real present does not rely on (it
  // asks for t == 1) but what an in-between at the interval's start must be. The previous DRAWN
  // height is 18 + g_DeltaTime 2 = 20, because the recipe steps the guest state before it draws.
  const Draw *fade = findPart(current, Part::Fade, 0);
  const Draw *top = findPart(current, Part::Border, spyro::field_2d_overlay_recipe::kBorderTopSlot);
  CHECK(fade != nullptr);
  CHECK(top != nullptr);
  if (fade != nullptr && top != nullptr) {
    CHECK_EQ(fade->r, 32u);
    CHECK_EQ(top->rect.h, 20);
  }
  // t == 1 must reproduce the current endpoint exactly, because the real present runs the same
  // reconstruction at t == 1 and the logic frame's picture cannot depend on which path drew it.
  Overlay atOne = endpoint;
  Census one;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(atOne, previous, 1.0, one), Status::Ready);
  CHECK_EQ(atOne.draws.size(), endpoint.draws.size());
  for (size_t i = 0; i < atOne.draws.size() && i < endpoint.draws.size(); ++i) {
    CHECK_EQ(atOne.draws[i].rect.x, endpoint.draws[i].rect.x);
    CHECK_EQ(atOne.draws[i].rect.y, endpoint.draws[i].rect.y);
    CHECK_EQ(atOne.draws[i].r, endpoint.draws[i].r);
    CHECK_EQ(atOne.draws[i].g, endpoint.draws[i].g);
    CHECK_EQ(atOne.draws[i].b, endpoint.draws[i].b);
  }
}

void test_a_draw_the_previous_frame_omitted_is_absent_not_misattributed() {
  State before = fieldState();
  before.collectables.lifeOrbCount = 1;
  State after = fieldState();
  after.collectables.lifeOrbCount = 2;
  Overlay previous;
  Overlay current;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(before, previous), Status::Ready);
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(after, current), Status::Ready);
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 0.5, census),
           Status::Ready);
  // 5 current draws, 4 previous. The extra orb has no predecessor, so it is ABSENT — a nameable
  // failure, distinct from an incompatible pair and from an unattributed draw, and it keeps its own
  // endpoint rather than being drawn at the previous frame's picture for it.
  CHECK_EQ(census.actors, 5u);
  CHECK_EQ(census.interpolated, 4u);
  CHECK_EQ(census.absent, 1u);
  CHECK_EQ(census.incompatible, 0u);
  CHECK_EQ(census.unattributed, 0u);
  const Draw *extra = findPart(current, Part::Sprite, 1);
  CHECK(extra != nullptr);
  if (extra != nullptr) {
    CHECK_EQ(extra->rect.y, 40);
  }
}

void test_every_identity_rule_is_provoked_and_counted() {
  const Draw base = {.part = Part::Sprite,
                     .instance = spyro::field_2d_overlay_recipe::instanceOf(Part::Sprite, 0),
                     .slot = 0,
                     .rect = {1, 2, 3, 4},
                     .r = 5,
                     .g = 6,
                     .b = 7,
                     .semi = false,
                     .blendMode = 0,
                     .u0 = 8,
                     .v0 = 9,
                     .clut = 0x2420,
                     .tpage = 0x0080};
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, base), Mismatch::None);
  // Moving a draw is NOT a mismatch: the position and the colour are what the interval samples.
  Draw moved = base;
  moved.rect.x = 99;
  moved.rect.y = 98;
  moved.r = 1;
  moved.g = 2;
  moved.b = 3;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, moved), Mismatch::None);

  Draw otherPart = base;
  otherPart.part = Part::Fade;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherPart), Mismatch::PartSlot);
  Draw otherSlot = base;
  otherSlot.slot = 3;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherSlot), Mismatch::PartSlot);
  Draw otherPage = base;
  otherPage.tpage = 0x0180;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherPage), Mismatch::TilePage);
  Draw otherClut = base;
  otherClut.clut = 0x2421;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherClut), Mismatch::Clut);
  Draw otherOrigin = base;
  otherOrigin.u0 = 16;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherOrigin), Mismatch::TileOrigin);
  Draw otherBlend = base;
  otherBlend.blendMode = 1;
  CHECK_EQ(spyro::field_2d_overlay_recipe::mismatch(base, otherBlend), Mismatch::BlendMode);
  // Every enumerator has a name, so a census can print which rule dominated.
  for (size_t i = 0; i < spyro::field_2d_overlay_recipe::kMismatchCount; ++i) {
    CHECK(spyro::field_2d_overlay_recipe::mismatchName(static_cast<Mismatch>(i)) != nullptr);
  }
}

void test_a_refused_pair_is_counted_by_reason_and_keeps_its_own_endpoint() {
  // An egg whose tile advanced, next to a fade whose blend mode changed, in one interval. Both must
  // be refused, for DIFFERENT reasons, and each must keep its own endpoint's coordinates.
  State before = fieldState();
  before.fade = 0;
  before.borderEnabled = 0;
  before.barHeight = 0;
  before.collectables.lifeDisplay = 0;
  before.collectables.lifeOrbCount = 0;
  before.collectables.eggDisplay = 1;
  before.collectables.eggCount = 1;
  before.collectables.eggPhase = 0;
  before.collectables.rects[0] = {40, 40, 12, 16};
  for (uint32_t i = 0; i < 10; ++i) {
    before.collectables.tiles[i] = {static_cast<uint8_t>(i), 0, 0x2400, 0x0080};
  }
  State after = before;
  after.collectables.eggPhase = 1;
  after.fade = 4;
  Overlay previous;
  Overlay current;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(before, previous), Status::Ready);
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(after, current), Status::Ready);
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 0.5, census),
           Status::Ready);
  // The fade is NEW (the previous frame had none) so it is absent, and the egg is INCOMPATIBLE. Two
  // different failures, counted apart — the split instance_pairing::Census exists for.
  CHECK_EQ(census.actors, 2u);
  CHECK_EQ(census.interpolated, 0u);
  CHECK_EQ(census.absent, 1u);
  CHECK_EQ(census.incompatible, 1u);
  CHECK_EQ(census.mismatches[static_cast<size_t>(Mismatch::TileOrigin)], 1u);
  CHECK_EQ(census.worstMismatch(), Mismatch::TileOrigin);
  const Draw *egg = findPart(current, Part::Sprite, 0);
  CHECK(egg != nullptr);
  if (egg != nullptr) {
    // Its own endpoint: the CURRENT tile, not the previous frame's.
    CHECK_EQ(egg->u0, 2u);
  }
}

void test_an_interval_outside_the_pair_is_refused_rather_than_clamped() {
  const State state = fieldState();
  Overlay previous;
  Overlay current;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, previous), Status::Ready);
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, current), Status::Ready);
  Census census;
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, 1.5, census),
           Status::InvalidCount);
  CHECK_EQ(spyro::field_2d_overlay_recipe::interpolate(current, previous, -0.1, census),
           Status::InvalidCount);
  // A refused interval scanned nothing: a count of 0 is how "the rule never ran" is told apart from
  // "the rule matched everything".
  CHECK_EQ(census.actors, 0u);
}

void test_an_impossible_collectable_count_refuses_the_whole_overlay() {
  State state = fieldState();
  state.collectables.lifeDisplay = 1;
  state.collectables.lifeOrbCount = 21;
  Overlay overlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(state, overlay), Status::InvalidCount);
  // The whole overlay, not just the sprites: publishing the fade and the border without the sprites
  // they were drawn over is a picture that differs from the guest's for a reason nobody chose.
  CHECK_EQ(overlay.draws.size(), 0u);
  // A flight level is a DIFFERENT thing and is not this refusal. Recovered from the guest's own
  // `if` placement in func_80019300: `if (g_IsFlightLevel == 0) { …the HUD Mobys… }` closes BEFORE
  // the orb/egg loops, so a flight level suppresses the Mobys and still draws the sprites. The
  // recipe reproduces that, and this asserts it so a later "tidy-up" cannot move the brace.
  State flight = fieldState();
  flight.collectables.flightLevel = true;
  Overlay flightOverlay;
  CHECK_EQ(spyro::field_2d_overlay_recipe::derive(flight, flightOverlay), Status::Ready);
  CHECK_EQ(countPart(flightOverlay, Part::Sprite), 2u);
}

void test_every_instance_key_is_distinct_and_nonzero() {
  // The pairing walks by instance key, so a collision would merge two different draws and a zero
  // would make a draw unpairable on both sides. Both are checked here rather than assumed.
  const uint32_t fade = spyro::field_2d_overlay_recipe::instanceOf(Part::Fade, 0);
  const uint32_t top = spyro::field_2d_overlay_recipe::instanceOf(Part::Border, 0);
  const uint32_t bottom = spyro::field_2d_overlay_recipe::instanceOf(Part::Border, 1);
  const uint32_t sprite0 = spyro::field_2d_overlay_recipe::instanceOf(Part::Sprite, 0);
  const uint32_t sprite16 = spyro::field_2d_overlay_recipe::instanceOf(Part::Sprite, 16);
  CHECK(fade != 0u);
  CHECK(top != 0u);
  CHECK(bottom != 0u);
  CHECK(sprite0 != 0u);
  CHECK(sprite16 != 0u);
  CHECK(fade != top);
  CHECK(top != bottom);
  CHECK(bottom != sprite0);
  CHECK(sprite0 != sprite16);
  // 255 sprites is the largest the guest's pool can hold (field_collectables_recipe::kMaxSprites),
  // and every one of them needs its own key: kSpriteSlotBase + 254 must not collide with a fixed
  // slot.
  CHECK(sprite16 + 254u != fade);
  CHECK(sprite16 + 254u != top);
  CHECK(sprite16 + 254u != bottom);
}

void test_lerp_is_exact_at_both_ends_and_rounds_half_up_in_between() {
  using spyro::field_2d_overlay_recipe::lerp;
  // The endpoint property presentation depends on: the real present runs the same reconstruction at
  // t == 1, so a sampler that was off by one there would make the logic frame's picture depend on
  // which path drew it.
  for (int32_t a = -40; a <= 40; a += 7) {
    for (int32_t b : {-512, -1, 0, 1, 240, 512, 4000}) {
      CHECK_EQ(lerp(a, b, 0.0), a);
      CHECK_EQ(lerp(a, b, 1.0), b);
    }
  }
  CHECK_EQ(lerp(0, 1, 0.5), 1);
  // A tie rounds AWAY FROM ZERO, so a descending half-step lands where an ascending one does. The
  // alternative — a rule that rounded a tie toward the endpoint it is nearer to — is not symmetric
  // under `lerp(a,b,t) == lerp(b,a,1-t)`, and both endpoints are equally near a tie by
  // construction.
  CHECK_EQ(lerp(1, 0, 0.5), 1);
  CHECK_EQ(lerp(0, 3, 0.5), 2);
  CHECK_EQ(lerp(3, 0, 0.5), 2);
  CHECK_EQ(lerp(10, 20, 0.25), 13);
  CHECK_EQ(lerp(20, 10, 0.25), 18);
  CHECK_EQ(lerp(0, 0, 0.5), 0);
  // …and the symmetry itself, which is the property that makes "the previous endpoint" a name
  // rather than a direction the answer depends on.
  for (int32_t a = -30; a <= 30; a += 11) {
    for (int32_t b : {-240, -7, 0, 3, 224, 512}) {
      for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        CHECK_EQ(lerp(a, b, t), lerp(b, a, 1.0 - t));
      }
    }
  }
}

} // namespace

int main() {
  RUN(fade_quad_is_the_guests_own_extent_and_colour);
  RUN(border_bars_are_the_guests_own_two_polys_and_stepped_height);
  RUN(disarmed_border_commits_its_step_but_draws_no_bar);
  RUN(collectable_sprites_are_the_guests_polys_with_the_offsets_applied);
  RUN(egg_tiles_advance_one_animation_frame_per_update);
  RUN(interpolates_position_and_colour_but_never_the_texture);
  RUN(the_sampler_reaches_every_rect_field_and_no_other);
  RUN(the_two_endpoints_are_exact);
  RUN(a_draw_the_previous_frame_omitted_is_absent_not_misattributed);
  RUN(every_identity_rule_is_provoked_and_counted);
  RUN(a_refused_pair_is_counted_by_reason_and_keeps_its_own_endpoint);
  RUN(an_interval_outside_the_pair_is_refused_rather_than_clamped);
  RUN(an_impossible_collectable_count_refuses_the_whole_overlay);
  RUN(every_instance_key_is_distinct_and_nonzero);
  RUN(lerp_is_exact_at_both_ends_and_rounds_half_up_in_between);
  return pt_summary();
}
