#include "actor_temporal.h"

#include "actor_pairing.h"

#include "actor_prefix_builder.h"
#include "actor_record_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using spyro::actor_recipe_capture::Record;
using namespace spyro::actor_pairing;
using spyro::actor_temporal::Endpoint;
using spyro::actor_temporal::History;

unsigned checks = 0;

void require(bool condition, const char *what) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "actor_temporal: %s (check %u)\n", what, checks);
    std::abort();
  }
}

// Exactly what a temporal source does with a pair: copy the current endpoint and sample it in
// place against its predecessor, through the shipping pairing owner. The endpoint's frame serial
// is the pair's bookkeeping, not the corpus's, so it plays no part here.
std::vector<Record>
sampleAt(const Endpoint &previous, const Endpoint &current, double t, Census &census) {
  std::vector<Record> sampled = current;
  sample(previous, sampled, t, census);
  return sampled;
}

void test_compatibility_names_every_identity_field() {
  const Record previous = spyro::test_fixture::actorRecord(0x80010000u, 0);
  const Record current = spyro::test_fixture::actorRecord(0x80010000u, 1024);
  require(compatible(previous, current), "a moved actor was not recognised as itself");

  const auto rejects = [&previous](const Record &candidate, Mismatch expected, const char *what) {
    require(mismatch(previous, candidate) == expected, what);
    require(!compatible(previous, candidate), what);
  };
  const auto accepts = [&previous](const Record &candidate, const char *what) {
    require(mismatch(previous, candidate) == Mismatch::None, what);
    require(compatible(previous, candidate), what);
  };

  Record remodelled = current;
  remodelled.descriptor = 0x80070100u;
  rejects(remodelled, Mismatch::Descriptor, "a changed model descriptor was paired or misnamed");

  Record grown = current;
  grown.input.vertexCount = 2;
  rejects(grown, Mismatch::VertexCount, "a changed vertex count was paired or misnamed");

  Record empty = current;
  empty.input.vertexCount = 0;
  Record emptyPrevious = previous;
  emptyPrevious.input.vertexCount = 0;
  require(mismatch(emptyPrevious, empty) == Mismatch::VertexCount,
          "a record with no vertices was paired");

  Record rescaledDepth = current;
  rescaledDepth.input.header = 0x01000000u;
  rejects(rescaledDepth, Mismatch::CoordShift, "a changed depth scale was paired or misnamed");

  Record clipped = current;
  clipped.input.header = 0x80000000u;
  rejects(clipped, Mismatch::CoordShift, "a changed clip mode was paired or misnamed");

  Record retopologised = current;
  retopologised.input.primitiveWords.push_back(0u);
  rejects(
      retopologised, Mismatch::PrimitiveCount, "a changed primitive count was paired or misnamed");

  // Everything below is per-endpoint state the sampler reads from the side it belongs to. An actor
  // that merely animated is still the same actor, and this is the rejection that cost three
  // quarters of the Artisans corpus when the whole header word had to match.
  Record animating = current;
  animating.input.header = 0x00007f00u;
  animating.input.alternate = animating.input.primary;
  accepts(animating, "an animating actor was refused as a different model");

  Record rekeyed = current;
  rekeyed.input.primary.firstFull ^= 0x00200000u;
  rekeyed.input.primary.fullWords[0] ^= 0x00200000u;
  accepts(rekeyed, "a different keyframe pose was refused as a different model");

  Record recoloured = current;
  recoloured.input.primaryColors = {0x00445566u};
  accepts(recoloured, "a recoloured actor was refused as a different model");

  require(compatible(previous, previous), "an actor that did not move was refused");
}

void test_endpoints_are_reproduced_exactly_and_the_midpoint_is_not() {
  // The actor moves both across the screen and away from the camera, so the sample has to move
  // the projected position AND the depth key it is sorted by.
  const auto previous = Endpoint{spyro::test_fixture::actorRecord(0x80010000u, 0, 4096)};
  const auto current = Endpoint{spyro::test_fixture::actorRecord(0x80010000u, 1024, 8192)};

  std::vector<Record> sampled;
  Census census{};

  sampled = sampleAt(previous, current, 1.0, census);
  require(census.actors == 1 && census.interpolated == 1, "t=1 did not interpolate the pair");
  require(
      spyro::actor_prefix::compareOutputs(current[0].expected, sampled[0].expected).mismatches == 0,
      "t=1 did not reproduce the current endpoint exactly");
  const int32_t atOne = sampled[0].expected.vertices[0].projected.sx;

  sampled = sampleAt(previous, current, 0.0, census);
  {
    // At t=0 the GEOMETRY is the previous endpoint's, exactly. The record's identity — its header,
    // colours, primitive words and transform snapshot — still comes from the current frame, which
    // is what makes the sample a member of the current frame's corpus rather than a replay of the
    // previous one.
    const auto &zero = sampled[0].expected;
    const auto &was = previous[0].expected;
    require(zero.vertices.size() == was.vertices.size(), "t=0 lost a vertex");
    for (size_t i = 0; i < zero.vertices.size(); ++i) {
      require(zero.vertices[i].projected.sx == was.vertices[i].projected.sx &&
                  zero.vertices[i].projected.sy == was.vertices[i].projected.sy &&
                  zero.vertices[i].scratchWord == was.vertices[i].scratchWord,
              "t=0 did not reproduce the previous endpoint's geometry exactly");
    }
    require(zero.controls[15] == was.controls[15], "t=0 did not reproduce the previous depth key");
  }
  const int32_t atZero = sampled[0].expected.vertices[0].projected.sx;
  require(atZero != atOne, "the fixture's endpoints are indistinguishable");

  sampled = sampleAt(previous, current, 0.5, census);
  require(census.interpolated == 1 && census.unpaired == 0 && census.incompatible == 0 &&
              census.refused == 0,
          "the midpoint was not interpolated");
  const int32_t atHalf = sampled[0].expected.vertices[0].projected.sx;
  // The discriminator. A sampler that quietly returned one endpoint would satisfy both exact
  // cases above and still show the actor teleporting once per logic frame.
  require(atHalf > std::min(atZero, atOne) && atHalf < std::max(atZero, atOne),
          "the midpoint is not strictly between the endpoints");
  const uint32_t depthAtHalf = sampled[0].expected.controls[15];
  const uint32_t depthAtZero = previous[0].expected.controls[15];
  const uint32_t depthAtOne = current[0].expected.controls[15];
  require(depthAtZero != depthAtOne, "the fixture's endpoints share a depth key");
  require(depthAtHalf > std::min(depthAtZero, depthAtOne) &&
              depthAtHalf < std::max(depthAtZero, depthAtOne),
          "the sampled depth key did not move with the sampled geometry");
}

void test_unpaired_actors_are_shown_at_their_own_endpoint() {
  const auto previous = Endpoint{spyro::test_fixture::actorRecord(0x80010000u, 0)};
  auto currentRecords = std::vector<Record>{spyro::test_fixture::actorRecord(0x80010000u, 1024),
                                            spyro::test_fixture::actorRecord(0x80010040u, 512)};
  const auto &current = currentRecords;

  std::vector<Record> sampled;
  Census census{};
  sampled = sampleAt(previous, current, 0.5, census);
  require(census.actors == 2, "the census lost a record");
  require(census.interpolated == 1 && census.unpaired == 1 && census.incompatible == 0 &&
              census.refused == 0,
          "an actor with no predecessor was not counted as unpaired");
  require(census.interpolated + census.unpaired + census.incompatible + census.refused ==
              census.actors,
          "the census does not account for every actor");
  require(spyro::actor_prefix::compareOutputs(currentRecords[1].expected, sampled[1].expected)
                  .mismatches == 0,
          "the unpaired actor was not shown at its own endpoint");

  // A record the producer could not attribute to an instance is unpairable on both sides.
  auto anonymousRecords = std::vector<Record>{spyro::test_fixture::actorRecord(0, 1024)};
  const auto &anonymous = anonymousRecords;
  sampled = sampleAt(anonymous, anonymous, 0.5, census);
  require(census.actors == 1 && census.unpaired == 1 && census.interpolated == 0,
          "a record with no instance identity was paired");

  // A predecessor that describes a different model is counted apart from having none at all, and
  // the census names the field that rejected it.
  auto recycledRecords = std::vector<Record>{spyro::test_fixture::actorRecord(0x80010000u, 1024)};
  recycledRecords[0].input.header = 0x01000000u;
  recycledRecords[0].expected = spyro::actor_prefix::build(recycledRecords[0].input);
  const auto &recycled = recycledRecords;
  sampled = sampleAt(previous, recycled, 0.5, census);
  require(census.actors == 1 && census.incompatible == 1 && census.unpaired == 0 &&
              census.interpolated == 0,
          "a recycled instance was not counted apart from an absent one");
  require(census.worstMismatch() == Mismatch::CoordShift,
          "the census did not name the field that rejected the pair");
  require(census.mismatches[(size_t)Mismatch::CoordShift] == 1,
          "the named reason lost its denominator");

  // A Moby drawn twice is two records, and identity alone cannot say which pose belongs to which
  // draw. Occurrence order can, so the second draw pairs only when last frame had a second draw.
  const auto doubled = Endpoint{spyro::test_fixture::actorRecord(0x80010000u, 0),
                                spyro::test_fixture::actorRecord(0x80010000u, 2048)};
  sampled = sampleAt(previous, doubled, 0.5, census);
  require(census.actors == 2 && census.interpolated == 1 && census.unpaired == 1,
          "a second draw of one instance reused the first draw's pose");
  sampled = sampleAt(doubled, doubled, 0.5, census);
  require(census.actors == 2 && census.interpolated == 2,
          "two draws of one instance were not paired in occurrence order");
  require(sampled[1].expected.vertices[0].projected.sx ==
              doubled[1].expected.vertices[0].projected.sx,
          "the second draw was paired against the first draw's pose");
}

void test_history_pairs_only_consecutive_frames_of_one_scene() {
  History history;
  history.begin(1, false, true);
  history.retain({spyro::test_fixture::actorRecord(0x80010000u, 0)});
  require(!history.paired(), "a single endpoint was treated as an interval");
  history.rotate();
  history.begin(1, false, true);
  history.retain({spyro::test_fixture::actorRecord(0x80010000u, 1024)});
  require(history.paired(), "two consecutive frames of one scene were not paired");
  require(history.previous() != nullptr && history.current() != nullptr,
          "an endpoint went missing");

  // A frame that never submitted leaves a gap, and a gap is not an interval: the frame that
  // follows it has nothing to interpolate from, however consecutive the serials look.
  history.rotate();
  history.begin(1, false, true);
  require(!history.paired(), "a frame with no submission was paired");
  history.rotate();
  history.begin(1, false, true);
  history.retain({spyro::test_fixture::actorRecord(0x80010000u, 2048)});
  require(!history.paired(), "an interval spanned a frame that drew no actors");

  // Two submissions in one logic frame compose one picture; half of it is not an endpoint.
  history.rotate();
  history.begin(1, false, true);
  history.retain({spyro::test_fixture::actorRecord(0x80010000u, 3072)});
  history.retain({spyro::test_fixture::actorRecord(0x80010040u, 0)});
  require(!history.paired(), "a doubly submitted frame was admitted");

  // A scene change drops the previous side outright.
  History scenes;
  scenes.begin(1, false, true);
  scenes.retain({spyro::test_fixture::actorRecord(0x80010000u, 0)});
  scenes.rotate();
  scenes.begin(2, false, true);
  scenes.retain({spyro::test_fixture::actorRecord(0x80010000u, 1024)});
  require(!scenes.paired(), "an interval spanned two scenes");

  // Reference frames and an inactive interpolation product retain nothing at all.
  History reference;
  reference.begin(1, true, true);
  reference.retain({spyro::test_fixture::actorRecord(0x80010000u, 0)});
  require(reference.current() == nullptr, "a reference frame became an endpoint");
  History inactive;
  inactive.begin(1, false, false);
  inactive.retain({spyro::test_fixture::actorRecord(0x80010000u, 0)});
  require(inactive.current() == nullptr, "an inactive product retained an endpoint");
}

} // namespace

int main() {
  test_compatibility_names_every_identity_field();
  test_endpoints_are_reproduced_exactly_and_the_midpoint_is_not();
  test_unpaired_actors_are_shown_at_their_own_endpoint();
  test_history_pairs_only_consecutive_frames_of_one_scene();
  std::printf("actor_temporal: %u checks passed\n", checks);
  return 0;
}
