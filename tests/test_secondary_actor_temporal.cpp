#include "secondary_actor_temporal.h"

#include "actor_prefix_builder.h"
#include "actor_record_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using spyro::actor_pairing::Census;
using spyro::actor_pairing::Mismatch;
using spyro::secondary_actor_temporal::Endpoint;
using spyro::secondary_actor_temporal::History;

unsigned checks = 0;

void require(bool condition, const char *what) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "secondary_actor_temporal: %s (check %u)\n", what, checks);
    std::abort();
  }
}

// One secondary-layer scene frame. The actor record is the shared fixture's, wrapped in the
// per-actor struct this layer draws from — which is the whole reason the pairing takes pointers.
Endpoint frameOf(std::initializer_list<std::pair<uint32_t, int32_t>> actors,
                 uint32_t lightingControl = 0x2020u) {
  Endpoint frame{};
  for (const auto &[moby, tx] : actors) {
    spyro::secondary_actor_scene::Record record{};
    record.moby = moby;
    record.lightingControl = lightingControl;
    record.actor = spyro::test_fixture::actorRecord(moby, tx);
    frame.visitedMobys.push_back(moby);
    frame.records.push_back(std::move(record));
  }
  return frame;
}

Endpoint sampleAt(const Endpoint &previous, const Endpoint &current, double t, Census &census) {
  Endpoint sampled = current;
  spyro::secondary_actor_temporal::sample(previous, sampled, t, census);
  return sampled;
}

// The discriminator for the nesting. A pairing that reached only the outer struct, or that paired
// by position in a vector of wrappers rather than by the inner record, would still reproduce both
// endpoints and would still count every actor; it would not move the pose at the midpoint.
void test_the_sample_reaches_the_nested_record() {
  const auto previous = frameOf({{0x80010000u, 0}});
  const auto current = frameOf({{0x80010000u, 1024}});
  Census census{};

  const auto atOne = sampleAt(previous, current, 1.0, census);
  require(census.actors == 1 && census.interpolated == 1, "t=1 did not interpolate the pair");
  require(spyro::actor_prefix::compareOutputs(current.records[0].actor.expected,
                                              atOne.records[0].actor.expected)
                  .mismatches == 0,
          "t=1 did not reproduce the current endpoint exactly");

  const auto atZero = sampleAt(previous, current, 0.0, census);
  const auto &was = previous.records[0].actor.expected;
  const auto &zero = atZero.records[0].actor.expected;
  require(zero.vertices.size() == was.vertices.size(), "t=0 lost a vertex");
  for (size_t i = 0; i < zero.vertices.size(); ++i) {
    require(zero.vertices[i].projected.sx == was.vertices[i].projected.sx &&
                zero.vertices[i].projected.sy == was.vertices[i].projected.sy,
            "t=0 did not reproduce the previous endpoint's geometry");
  }

  const auto atHalf = sampleAt(previous, current, 0.5, census);
  const int32_t one = atOne.records[0].actor.expected.vertices[0].projected.sx;
  const int32_t zeroX = zero.vertices[0].projected.sx;
  const int32_t half = atHalf.records[0].actor.expected.vertices[0].projected.sx;
  require(zeroX != one, "the fixture's endpoints share a projected position");
  require(half > std::min(zeroX, one) && half < std::max(zeroX, one),
          "the midpoint is not strictly between the endpoints");
}

// The recipe reads the frame's own state per record, so an endpoint that kept only a record corpus
// would reconstruct a differently lit, differently shadowed picture than the logic frame did.
void test_the_endpoint_is_the_whole_scene_frame() {
  auto previous = frameOf({{0x80010000u, 0}}, 0x2020u);
  auto current = frameOf({{0x80010000u, 1024}}, 0x4040u);
  current.shadows.push_back({0x80010000u, 7u});
  Census census{};

  const auto sampled = sampleAt(previous, current, 0.5, census);
  require(sampled.records[0].lightingControl == 0x4040u,
          "the sample lost the current frame's lighting control word");
  require(sampled.shadows.size() == 1 && sampled.shadows[0].modelByte == 7u,
          "the sample lost the current frame's shadow list");
  require(sampled.visitedMobys == current.visitedMobys, "the sample lost the visited-Moby list");
  require(previous.records[0].actor.expected.vertices[0].projected.sx !=
              sampled.records[0].actor.expected.vertices[0].projected.sx,
          "the sample returned the previous endpoint unchanged");
}

void test_unpaired_and_recycled_actors_are_counted_apart() {
  const auto previous = frameOf({{0x80010000u, 0}});
  const auto current = frameOf({{0x80010000u, 1024}, {0x80010040u, 512}});
  Census census{};

  const auto sampled = sampleAt(previous, current, 0.5, census);
  require(census.actors == 2, "the census lost a record");
  require(census.interpolated == 1 && census.unpaired == 1 && census.incompatible == 0,
          "an actor with no predecessor was not counted as unpaired");
  require(census.interpolated + census.unpaired + census.incompatible + census.refused ==
              census.actors,
          "the census does not account for every actor");
  require(spyro::actor_prefix::compareOutputs(current.records[1].actor.expected,
                                              sampled.records[1].actor.expected)
                  .mismatches == 0,
          "the unpaired actor was not shown at its own endpoint");

  auto recycled = frameOf({{0x80010000u, 1024}});
  recycled.records[0].actor.input.header = 0x01000000u;
  recycled.records[0].actor.expected = spyro::actor_prefix::build(recycled.records[0].actor.input);
  sampleAt(previous, recycled, 0.5, census);
  require(census.actors == 1 && census.incompatible == 1 && census.unpaired == 0 &&
              census.interpolated == 0,
          "a recycled instance was not counted apart from an absent one");
  require(census.worstMismatch() == Mismatch::CoordShift,
          "the census did not name the field that rejected the pair");
}

void test_history_pairs_only_consecutive_frames_of_one_scene() {
  History history;
  history.begin(1, false, true);
  history.retain(frameOf({{0x80010000u, 0}}));
  require(!history.paired(), "a single endpoint was treated as an interval");
  history.rotate();
  history.begin(1, false, true);
  history.retain(frameOf({{0x80010000u, 1024}}));
  require(history.paired(), "two consecutive frames of one scene were not paired");

  // A frame the producer refused leaves a gap, and a gap is not an interval.
  history.rotate();
  history.begin(1, false, true);
  history.refuse();
  history.retain(frameOf({{0x80010000u, 2048}}));
  require(!history.paired(), "a refused frame became an endpoint");

  history.rotate();
  history.begin(1, false, true);
  history.retain(frameOf({{0x80010000u, 3072}}));
  require(!history.paired(), "an interval spanned a refused frame");

  // Two submissions in one logic frame compose one picture; half of it is not an endpoint.
  history.rotate();
  history.begin(1, false, true);
  history.retain(frameOf({{0x80010000u, 4096}}));
  history.retain(frameOf({{0x80010040u, 0}}));
  require(!history.paired(), "a doubly submitted frame was admitted");

  History scenes;
  scenes.begin(1, false, true);
  scenes.retain(frameOf({{0x80010000u, 0}}));
  scenes.rotate();
  scenes.begin(2, false, true);
  scenes.retain(frameOf({{0x80010000u, 1024}}));
  require(!scenes.paired(), "an interval spanned two scenes");

  History reference;
  reference.begin(1, true, true);
  reference.retain(frameOf({{0x80010000u, 0}}));
  require(reference.current() == nullptr, "a reference frame became an endpoint");
  History inactive;
  inactive.begin(1, false, false);
  inactive.retain(frameOf({{0x80010000u, 0}}));
  require(inactive.current() == nullptr, "an inactive product retained an endpoint");
}

} // namespace

int main() {
  test_the_sample_reaches_the_nested_record();
  test_the_endpoint_is_the_whole_scene_frame();
  test_unpaired_and_recycled_actors_are_counted_apart();
  test_history_pairs_only_consecutive_frames_of_one_scene();
  std::printf("secondary_actor_temporal: %u checks passed\n", checks);
  return 0;
}
