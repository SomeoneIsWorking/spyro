#include "core.h"
#include "fps60.h"
#include "game.h"
#include "spyro_context.h"
#include "temporal_scene.h"
#include "temporal_scene_source.h"
#include "testutil.h"
#include "world_scene_builder.h"
#include "world_source_fixture.h"
#include "world_temporal.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace {

using spyro::world_source::Source;
using spyro::world_temporal::History;
namespace fixture = spyro::testing::world_source_fixture;

struct Fixture {
  std::unique_ptr<Game> game = std::make_unique<Game>();
  SpyroContext context;
  Source source;
  spyro::world_scene_submitter::DrawState draw;
  std::vector<psx::cpu::ImageIdentity> images;

  Fixture() {
    auto &core = game->core;
    core.gameCtx = &context;
    game->mods.fps60 = true;
    game->mods.aspect = ASPECT_4_3;
    game->gpu.s_disp_w = 512;
    core.rsub.mode.setPath(RenderPath::Native);
    const auto bytes = fixture::lowGeometryFixture();
    std::copy(bytes.begin(), bytes.end(), std::begin(core.ram));
    core.rsub.projParams.setGeomOffset(256, 120);
    core.rsub.projParams.setGeomScreen(341);
    core.rsub.projParams.setProjH(341);
    game->gpu.s_da_x1 = 511;
    game->gpu.s_da_y1 = 239;
    source = spyro::world_scene::capture(&core, 0);
    CHECK(source.selection.valid);
    CHECK_EQ(spyro::world_scene::build(source).faces.size(), 2u);
    const auto captured = spyro::world_scene_submitter::captureDrawState(core);
    CHECK(captured.has_value());
    draw = *captured;
    for (const auto range : source.resourceRanges()) {
      images.push_back(core.imageCatalog().activate("synthetic world", range, 1u));
    }
    CHECK(!images.empty());
  }

  void retain(Source next) {
    context.worldTemporal.begin(1u, false, true);
    CHECK(context.worldTemporal.retain(game->core, std::move(next), draw));
  }

  void interval(const Source &previous, const Source &current) {
    retain(previous);
    context.worldTemporal.rotate();
    retain(current);
  }
};

void test_owned_source_and_current_destination() {
  Fixture f;
  auto previous = f.source, current = f.source;
  previous.selection.camera.position[1] = 128 * 16;
  current.selection.camera.position[1] = 192 * 16;
  f.retain(previous);
  f.context.worldTemporal.rotate();
  f.draw.offsetY = 256;
  f.draw.areaTop = 256;
  f.draw.areaBottom = 495;
  f.retain(current);
  auto &core = f.game->core;
  const char *why = nullptr;
  CHECK(f.context.worldTemporal.compatible(core, why));
  std::fill(std::begin(core.ram), std::end(core.ram), 0);
  core.rsub.projParams.setGeomOffset(0, 0);
  core.rsub.projParams.setProjH(7);
  f.game->gpu.s_off_y = 17;
  f.game->gpu.s_da_y0 = 0;
  f.game->gpu.s_da_y1 = 2;
  const std::vector<uint8_t> before(std::begin(core.ram), std::end(core.ram));
  spyro_temporal_scene_prepare(core);
  CHECK(f.context.worldTemporal.eligible);
  CHECK_EQ(f.context.pairedActor.temporal.calls, 0u);
  CHECK_EQ(core.rsub.projParams.projH(), 7u);
  CHECK_EQ(core.rsub.projParams.geomOfx(), 0.0f);
  CHECK_EQ(core.rsub.projParams.geomOfy(), 0.0f);
  CHECK_EQ(f.game->gpu.s_off_y, 17);
  CHECK_EQ(f.game->gpu.s_da_y1, 2);
  CHECK_EQ(f.game->rq.n, 0);
  auto target = std::make_unique<RenderQueue>();
  target->game = f.game.get();
  for (double t : {0.0, 0.5, 1.0}) {
    target->reset();
    CHECK(f.context.worldTemporal.emit(core, *target, t));
    CHECK_EQ(target->n, 2);
    const auto recipe = spyro::world_scene::sample(previous, current, t);
    CHECK_EQ(target->items[0].ysf[0], recipe.faces[0].vertices[0].screenY + 256.0f);
    CHECK_EQ(target->items[0].painter_object, spyro::world_temporal::kProducerKey);
    CHECK_EQ(target->items[0].da_y0, 256);
    CHECK_EQ(target->items[0].da_y1, 495);
  }
  for (double t : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
    target->reset();
    CHECK(!f.context.worldTemporal.emit(core, *target, t));
    CHECK_EQ(target->n, 0);
  }
  CHECK(std::equal(before.begin(), before.end(), std::begin(core.ram)));
  CHECK(f.context.worldTemporal.previous()->source.selection.camera.position ==
        previous.selection.camera.position);
  CHECK(f.context.worldTemporal.current()->source.selection.camera.position ==
        current.selection.camera.position);
}

void test_residency_checks_every_span_and_generation() {
  Fixture f;
  auto &core = f.game->core;
  auto &history = f.context.worldTemporal;
  f.interval(f.source, f.source);
  const char *why = nullptr;
  CHECK(history.compatible(core, why));
  const auto ranges = f.source.resourceRanges();
  const auto last = ranges.back();
  CHECK(core.imageCatalog().deactivate(f.images.back()));
  CHECK(!history.compatible(core, why));
  history.begin(1, false, true);
  CHECK(!history.retain(core, f.source, f.draw));
  CHECK(history.current() == nullptr);
  // A partial newest image cannot certify a full retained resource even while an older image
  // covers that resource's first byte.
  core.imageCatalog().activate("synthetic full reload", last, 1u);
  const auto partial =
      core.imageCatalog().activate("synthetic partial overwrite", {last.end - 1u, last.end}, 2u);
  CHECK(core.currentImageIdentity(last.begin).has_value());
  history.begin(1, false, true);
  CHECK(!history.retain(core, f.source, f.draw));
  CHECK(core.imageCatalog().deactivate(partial));
  history.rotate();
  f.interval(f.source, f.source);
  CHECK(history.compatible(core, why));
  core.imageCatalog().activate("synthetic equal-byte reload", last, 1u);
  CHECK(!history.compatible(core, why));
  spyro_temporal_scene_prepare(core);
  CHECK(!history.eligible);
}

void test_lifecycle_refuses_stale_or_duplicate_sources() {
  Fixture f;
  auto &history = f.context.worldTemporal;
  const char *why = nullptr;
  f.interval(f.source, f.source);
  CHECK(history.compatible(f.game->core, why));
  history.rotate();
  history.begin(1, false, true);
  history.rotate(); // Missing producer clears the previous endpoint.
  CHECK(history.previous() == nullptr);
  f.interval(f.source, f.source);
  history.refuse();
  history.rotate();
  CHECK(history.previous() == nullptr);
  for (const auto scene : {2u, 3u}) {
    f.interval(f.source, f.source);
    history.begin(scene, false, true);
    CHECK(history.previous() == nullptr);
  }
  for (bool reference : {false, true}) {
    f.interval(f.source, f.source);
    history.begin(1, reference, reference);
    CHECK(history.previous() == nullptr);
    CHECK(!history.retain(f.game->core, f.source, f.draw));
    history.begin(1, false, true);
    CHECK(history.previous() == nullptr);
  }
  f.retain(f.source);
  CHECK(!history.retain(f.game->core, f.source, f.draw));
  CHECK(history.current() == nullptr);
  history.rotate();
  CHECK(history.previous() == nullptr);
}

void test_draw_policy_changes_refuse_without_losing_destination_ownership() {
  Fixture f;
  auto &history = f.context.worldTemporal;
  f.retain(f.source);
  history.rotate();
  ++f.draw.windowOffsetX;
  f.retain(f.source);
  const char *why = nullptr;
  CHECK(!history.compatible(f.game->core, why));
  spyro_temporal_scene_prepare(f.game->core);
  CHECK(!history.eligible);
  history.begin(1, false, true);
  f.draw.areaLeft = f.draw.areaRight + 1;
  CHECK(!history.retain(f.game->core, f.source, f.draw));
  CHECK(history.current() == nullptr);
}

void test_midpoint_visibility_admission_and_refusal() {
  Fixture f;
  auto previous = f.source, current = f.source;
  previous.selection.camera.position[1] = -900 * 16;
  current.selection.camera.position[1] = 900 * 16;
  CHECK(spyro::world_scene::build(previous).status == spyro::world_recipe::Status::ValidEmpty);
  CHECK(spyro::world_scene::build(current).status == spyro::world_recipe::Status::ValidEmpty);
  f.game->gpu.s_off_y = 256;
  f.game->gpu.s_da_y0 = 256;
  f.game->gpu.s_da_y1 = 495;
  const auto emptyPlan = spyro::world_scene_submitter::prepare(&f.game->core,
                                                               f.game->rq,
                                                               spyro::world_temporal::kProducerKey,
                                                               spyro::world_scene::build(current));
  CHECK(emptyPlan.status == spyro::world_scene_submitter::Status::ValidEmpty);
  CHECK_EQ(emptyPlan.draw.offsetY, 256);
  CHECK_EQ(emptyPlan.draw.areaBottom, 495);
  f.draw = emptyPlan.draw;
  f.interval(previous, current);
  spyro_temporal_scene_prepare(f.game->core);
  CHECK(f.context.worldTemporal.eligible);
  auto target = std::make_unique<RenderQueue>();
  target->game = f.game.get();
  CHECK(f.context.worldTemporal.emit(f.game->core, *target, 0.5));
  CHECK_EQ(target->n, 2);
  const auto midpoint = spyro::world_scene::sample(previous, current, 0.5);
  // Negative packed X borrows into endpoint Y before interpolation. Destination ownership is
  // the added draw offset, not an assumption that the source vertex lies at projection center.
  CHECK_EQ(target->items[0].ysf[0], midpoint.faces[0].vertices[0].screenY + 256.0f);
  Fps60 presentation(*f.game, spyro_temporal_scene_source(*f.game));
  for (float t : {0.0f, 0.5f, 1.0f}) {
    presentation.presentPass(&f.game->core, t, {});
    CHECK_EQ(presentation.mSink->n, t == 0.5f ? 2 : 0);
    CHECK_EQ(presentation.mPresentStream.size(), t == 0.5f ? 2u : 0u);
  }
  // An unadvanced channel or malformed inactive HQ may be invisible at both endpoints.
  // Both must reject the entire interval when the midpoint would select that source.
  for (bool animation : {true, false}) {
    auto badPrevious = previous, badCurrent = current;
    if (animation) {
      badPrevious.selection.sectors[0]->animation = 0xffffff00u;
    } else {
      // Midpoint z equals the LOD boundary. Radius one crosses the strict z-radius < lod
      // selection rule; radius zero would leave the deliberately invalid HQ dormant.
      badPrevious.selection.sectors[0]->extent = 1;
      badCurrent.selection.sectors[0]->extent = 1;
    }
    CHECK(spyro::world_scene::sample(badPrevious, badCurrent, 0.5).status ==
          (animation ? spyro::world_recipe::Status::ActiveAnimation
                     : spyro::world_recipe::Status::InvalidChunk));
    f.context.worldTemporal.rotate();
    f.interval(badPrevious, badCurrent);
    spyro_temporal_scene_prepare(f.game->core);
    CHECK(!f.context.worldTemporal.eligible);
    CHECK(spyro::world_scene::build(badPrevious).status == spyro::world_recipe::Status::ValidEmpty);
    CHECK(spyro::world_scene::build(badCurrent).status == spyro::world_recipe::Status::ValidEmpty);
  }
}

} // namespace

int main() {
  RUN(owned_source_and_current_destination);
  RUN(residency_checks_every_span_and_generation);
  RUN(lifecycle_refuses_stale_or_duplicate_sources);
  RUN(midpoint_visibility_admission_and_refusal);
  RUN(draw_policy_changes_refuse_without_losing_destination_ownership);
  return pt_summary();
}
