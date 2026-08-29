#include "scene_painter_order.h"
#include "testutil.h"

namespace {

void test_actor_world_terrain_splice_order() {
  const auto world = spyro::scene_painter_order::world(2047, 4, 0);
  const auto queued0 = spyro::scene_painter_order::queuedWorld(2047, 0);
  const auto queued1 = spyro::scene_painter_order::queuedWorld(2047, 1);
  const auto actor0 = spyro::scene_painter_order::actor(2047, 0, 0);
  const auto actor1 = spyro::scene_painter_order::actor(2047, 1, 0);
  const auto secondary0 = spyro::scene_painter_order::secondaryActor(2047, 0, 0);
  const auto secondary1 = spyro::scene_painter_order::secondaryActor(2047, 1, 0);
  const auto paired0 = spyro::scene_painter_order::pairedActor(2047, 0);
  const auto paired1 = spyro::scene_painter_order::pairedActor(2047, 1);
  const auto shadow0 = spyro::scene_painter_order::spyroShadow(2047, 0);
  const auto shadow1 = spyro::scene_painter_order::spyroShadow(2047, 1);
  const auto terrain0 = spyro::scene_painter_order::cyclorama(0);
  const auto terrain1 = spyro::scene_painter_order::cyclorama(1);
  CHECK_EQ(world.domain, spyro::scene_painter_order::kActorWorldTerrainDomain);
  CHECK(painterReplayBefore(world, queued1));
  CHECK(painterReplayBefore(queued1, queued0));
  CHECK(painterReplayBefore(queued0, actor0));
  CHECK(painterReplayBefore(actor0, actor1));
  CHECK(painterReplayBefore(actor1, secondary0));
  CHECK(painterReplayBefore(secondary0, secondary1));
  CHECK(painterReplayBefore(secondary1, paired0));
  CHECK(painterReplayBefore(paired0, paired1));
  CHECK(painterReplayBefore(paired1, shadow0));
  CHECK(painterReplayBefore(shadow0, shadow1));
  CHECK(painterReplayBefore(shadow1, terrain0));
  CHECK(painterReplayBefore(terrain0, terrain1));
}

void test_ot_bin_precedes_link_phase() {
  const auto fartherWorld = spyro::scene_painter_order::world(2047, 100, 0);
  const auto nearerTerrain =
      PainterReplayOrder{spyro::scene_painter_order::kActorWorldTerrainDomain,
                         {2046, spyro::scene_painter_order::cyclorama(0).key.link_ordinal, 0}};
  CHECK(painterReplayBefore(fartherWorld, nearerTerrain));
  const auto invalid = spyro::scene_painter_order::world(0, 1u << 30u, 0);
  CHECK(!invalid.authored());
}

} // namespace

int main() {
  RUN(actor_world_terrain_splice_order);
  RUN(ot_bin_precedes_link_phase);
  return pt_summary();
}
