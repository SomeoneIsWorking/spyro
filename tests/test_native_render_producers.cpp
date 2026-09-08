#include "core.h"
#include "fx_actor_draw.h"
#include "game.h"
#include "hw_bind.h"
#include "spyro_game.h"
#include "testutil.h"
#include "world_scene_submitter.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace {

constexpr uint32_t kLevelMobys = 0x80075828u;
constexpr uint32_t kMoby = 0x80010000u;
constexpr uint32_t kShadowCursor = 0x80075f00u;
constexpr uint32_t kShadowStart = 0x800724f4u;
constexpr uint32_t kPreviousCursor = kShadowStart + 16u;

void test_empty_actor_submission_commits_shadow_reset() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  core.mem_w32(kLevelMobys, kMoby);
  core.mem_w32(kMoby + 0x48u, 0xffffffffu);
  core.mem_w32(kShadowCursor, kPreviousCursor);
  core.mem_w32(kShadowStart, 0x80012000u);

  CHECK(spyro_actor_submit(&core));
  CHECK_EQ(core.mem_r32(kShadowCursor), kShadowStart);
  CHECK_EQ(game->rq.n, 0);
  // The cursor ends the list; clearing unrelated backing bytes is not part of this transition.
  CHECK_EQ(core.mem_r32(kShadowStart), 0x80012000u);
}

void test_refused_actor_submission_preserves_shadow_state() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  core.mem_w32(kLevelMobys, 0x801ffffcu);
  core.mem_w32(kShadowCursor, kPreviousCursor);
  core.mem_w32(kShadowStart, 0x80012000u);

  CHECK(!spyro_actor_submit(&core));
  CHECK_EQ(core.mem_r32(kShadowCursor), kPreviousCursor);
  CHECK_EQ(core.mem_r32(kShadowStart), 0x80012000u);
  CHECK_EQ(game->rq.n, 0);
}

constexpr uint32_t kMatrix = 0x80011000u;
constexpr uint32_t kObjectList = 0x80012000u;
constexpr uint32_t kObject = 0x80013000u;
constexpr uint32_t kFace = kObject + 40u;
constexpr uint32_t kScratchVertex = 0x1f80000cu;

void prepare_terrain(Game &game) {
  Core &core = game.core;
  game.mods.aspect = ASPECT_4_3;
  gte_bind(&core);
  for (uint32_t reg = 0; reg < 32u; ++reg) {
    gte_write_ctrl(reg, 0u);
  }
  gte_write_ctrl(24u, 256u << 16u);
  gte_write_ctrl(25u, 120u << 16u);
  gte_write_ctrl(26u, 341u);
  const std::array<uint32_t, 5> identity{4096u, 0u, 4096u, 0u, 4096u};
  for (uint32_t i = 0; i < identity.size(); ++i) {
    core.mem_w32(kMatrix + i * 4u, identity[i]);
  }
  core.mem_w32(0x80078a40u, 1u);
  core.mem_w32(0x80078a44u, kObjectList);
  core.mem_w32(kObjectList, kObject);
  core.mem_w32(kObject, 0u);
  core.mem_w32(kObject + 4u, (1000u << 16u) | 1u);
  core.mem_w32(kObject + 8u, 0u);
  core.mem_w32(kObject + 12u, (1000u << 16u) | 2u); // Three vertices.
  core.mem_w32(kObject + 16u, (8u << 14u) | 1u);    // One eight-byte face.
  core.mem_w32(kObject + 24u, 0u);
  core.mem_w32(kObject + 28u, 100u);
  core.mem_w32(kObject + 32u, 100u << 10u);
  core.mem_w32(kObject + 36u, 0x10112233u);
  core.mem_w32(kFace, (4u << 10u) | 8u);
  core.mem_w32(kFace + 4u, (4u << 20u) | (4u << 10u) | 4u);
  core.mem_w32(0x800757b0u, 0x80040000u);
  core.mem_w32(0x80075780u, 0x80050000u);
  game.gpu.s_da_x0 = 0;
  game.gpu.s_da_y0 = 0;
  game.gpu.s_da_x1 = 511;
  game.gpu.s_da_y1 = 239;
}

void test_owned_terrain_vertices_submit() {
  auto game = std::make_unique<Game>();
  prepare_terrain(*game);
  CHECK(spyro_terrain_submit(&game->core, -1, kMatrix, kMatrix));
  CHECK_EQ(game->rq.n, 1);
  CHECK_EQ(game->rq.items[0].painter_object, 0x8004eba8u);
}

void test_external_terrain_vertices_refuse_independently_of_scratch() {
  for (const uint32_t scratchClip : {0u, 0x1fu}) {
    auto game = std::make_unique<Game>();
    prepare_terrain(*game);
    Core &core = game->core;
    // Index three is outside the three native vertices. The retired path read this scratch slot
    // and accepted the face as clipped when all five clip flags were present.
    core.mem_w32(kFace, (12u << 20u) | (12u << 10u) | 12u);
    core.mem_w32(kScratchVertex, scratchClip);
    CHECK(!spyro_terrain_submit(&core, -1, kMatrix, kMatrix));
    CHECK_EQ(game->rq.n, 0);
    CHECK_EQ(core.mem_r32(kScratchVertex), scratchClip);
    CHECK_EQ(gte_read_ctrl(24u), 256u << 16u);
  }
}

constexpr uint32_t kWorldProducer = 0x800258f0u;
constexpr uint32_t kBroadVisibility = 0x800771c8u;

spyro::world_recipe::Recipe worldRecipe(bool visible) {
  spyro::world_recipe::Recipe recipe{};
  recipe.broadVisible[3] = 0xffu;
  recipe.broadVisible[255] = 0xffu;
  if (visible) {
    spyro::world_recipe::Face face{};
    face.otBin = 100u;
    face.paintGroup = 0u;
    face.vertices[0] = {.sx = 200, .sy = 100, .screenX = 200.25f, .screenY = 100.5f};
    face.vertices[1] = {.sx = 300, .sy = 100, .screenX = 300.25f, .screenY = 100.5f};
    face.vertices[2] = {.sx = 250, .sy = 200, .screenX = 250.25f, .screenY = 200.5f};
    for (auto &vertex : face.vertices) {
      vertex.sz = 1000u;
      vertex.viewZ = 1000.0f;
      vertex.rgb = 0x00112233u;
    }
    recipe.faces.push_back(face);
    recipe.status = spyro::world_recipe::Status::Ready;
  }
  return recipe;
}

void prepareWorldSubmission(Game &game) {
  game.mods.aspect = ASPECT_4_3;
  game.core.rsub.mode.setPath(RenderPath::Native);
  game.core.rsub.projParams.setGeomOffset(256, 120);
  game.core.rsub.projParams.setGeomScreen(341);
  game.gpu.s_da_x1 = 511;
  game.gpu.s_da_y1 = 239;
  for (uint32_t i = 0; i < 256u; ++i) {
    game.core.mem_w8(kBroadVisibility + i, 0x5au);
  }
}

void test_world_logic_submission_publishes_complete_visibility() {
  for (bool visible : {false, true}) {
    auto game = std::make_unique<Game>();
    prepareWorldSubmission(*game);
    const auto recipe = worldRecipe(visible);
    const auto plan =
        spyro::world_scene_submitter::prepare(&game->core, game->rq, kWorldProducer, recipe);
    CHECK(plan.status == (visible ? spyro::world_scene_submitter::Status::Ready
                                  : spyro::world_scene_submitter::Status::ValidEmpty));
    spyro::world_scene_submitter::submit(&game->core, game->rq, kWorldProducer, recipe, plan);
    for (uint32_t i = 0; i < recipe.broadVisible.size(); ++i) {
      CHECK_EQ(game->core.mem_r8(kBroadVisibility + i), recipe.broadVisible[i]);
    }
    CHECK_EQ(game->rq.n, visible ? 1 : 0);
    if (game->rq.n == 1) {
      const auto &item = game->rq.items[0];
      CHECK_EQ(item.painter_object, kWorldProducer);
      CHECK_EQ(item.painter_replay.key.ot_bin, 100u);
      CHECK_EQ(item.xsf[0], 200.25f);
      CHECK_EQ(item.ysf[0], 100.5f);
      CHECK_EQ(item.rs[0], 0x33u);
    }
  }
}

void test_refused_world_submission_preserves_visibility_and_queue() {
  auto game = std::make_unique<Game>();
  prepareWorldSubmission(*game);
  auto recipe = worldRecipe(true);
  recipe.faces.front().vertexCount = 4;
  const auto plan =
      spyro::world_scene_submitter::prepare(&game->core, game->rq, kWorldProducer, recipe);
  CHECK(plan.status == spyro::world_scene_submitter::Status::InvalidOrder);
  const std::vector<uint8_t> before(std::begin(game->core.ram), std::end(game->core.ram));
  spyro::world_scene_submitter::submit(&game->core, game->rq, kWorldProducer, recipe, plan);
  CHECK(std::equal(before.begin(), before.end(), std::begin(game->core.ram)));
  CHECK_EQ(game->rq.n, 0);
  CHECK(!spyro::world_scene_submitter::emit(&game->core, game->rq, kWorldProducer, recipe, plan));
  CHECK(std::equal(before.begin(), before.end(), std::begin(game->core.ram)));
  CHECK_EQ(game->rq.n, 0);
}

void test_world_presentation_emits_without_guest_writes() {
  for (bool visible : {false, true}) {
    auto game = std::make_unique<Game>();
    prepareWorldSubmission(*game);
    const auto recipe = worldRecipe(visible);
    const auto plan =
        spyro::world_scene_submitter::prepare(&game->core, game->rq, kWorldProducer, recipe);
    const std::vector<uint8_t> ramBefore(std::begin(game->core.ram), std::end(game->core.ram));
    const std::vector<uint8_t> scratchBefore(std::begin(game->core.scratch),
                                             std::end(game->core.scratch));
    CHECK(spyro::world_scene_submitter::emit(&game->core, game->rq, kWorldProducer, recipe, plan));
    CHECK(std::equal(ramBefore.begin(), ramBefore.end(), std::begin(game->core.ram)));
    CHECK(std::equal(scratchBefore.begin(), scratchBefore.end(), std::begin(game->core.scratch)));
    CHECK_EQ(game->rq.n, visible ? 1 : 0);
    if (game->rq.n == 1) {
      const RqItem display = game->rq.items[0];
      game->rq.reset();
      spyro::world_scene_submitter::submit(&game->core, game->rq, kWorldProducer, recipe, plan);
      CHECK_EQ(game->rq.n, 1);
      const auto &endpoint = game->rq.items[0];
      CHECK_EQ(display.painter_object, endpoint.painter_object);
      CHECK_EQ(display.painter_replay.key.ot_bin, endpoint.painter_replay.key.ot_bin);
      CHECK(std::equal(std::begin(display.xsf), std::end(display.xsf), std::begin(endpoint.xsf)));
      CHECK(std::equal(std::begin(display.ysf), std::end(display.ysf), std::begin(endpoint.ysf)));
      CHECK(std::equal(std::begin(display.rs), std::end(display.rs), std::begin(endpoint.rs)));
    }
  }
}

} // namespace

int main() {
  RUN(empty_actor_submission_commits_shadow_reset);
  RUN(refused_actor_submission_preserves_shadow_state);
  RUN(owned_terrain_vertices_submit);
  RUN(external_terrain_vertices_refuse_independently_of_scratch);
  RUN(world_logic_submission_publishes_complete_visibility);
  RUN(refused_world_submission_preserves_visibility_and_queue);
  RUN(world_presentation_emits_without_guest_writes);
  return pt_summary();
}
