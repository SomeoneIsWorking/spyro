// The terrain producer's temporal source, tested at the two things only it owns: which of the
// previous game update's objects is this object, and which field rejected a pair that could not be
// sampled.
//
// The endpoint lifecycle is `spyro::temporal::Pair` and the occurrence-ordered walk is
// `spyro::instance_pairing`; both are exercised where they live. The interval reaching the
// projection is exercised in the recipe's own tests, over the recipe's own corpus.
#include "terrain_temporal.h"

#include "testutil.h"

#include <string_view>
#include <vector>

namespace {

using namespace spyro::terrain_recipe;
namespace temporal = spyro::terrain_temporal;

// `marker` distinguishes two draws of the same guest object, so a test can say WHICH predecessor a
// pairing chose rather than only that it chose one.
Object objectAt(uint32_t address, int16_t marker, size_t vertices = 3, size_t faces = 1) {
  Object object{};
  object.address = address;
  for (size_t i = 0; i < vertices; ++i) {
    object.vertices.push_back({.x = (int16_t)(marker + (int16_t)i), .y = 0, .z = 1000});
  }
  for (size_t i = 0; i < faces; ++i) {
    Object::Face face{};
    face.index = {0, 4, 8};
    object.faces.push_back(face);
  }
  return object;
}

Input frameOf(std::initializer_list<Object> objects) {
  Input input{};
  input.view = {.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}}, .t = {}};
  input.projection = {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341};
  input.rightClip = 512;
  input.objects = objects;
  return input;
}

void test_pairing_is_by_object_in_occurrence_order(void) {
  const Input previous =
      frameOf({objectAt(0x80300000u, 10), objectAt(0x80400000u, 50), objectAt(0x80300000u, 20)});
  const Input current =
      frameOf({objectAt(0x80300000u, 11), objectAt(0x80300000u, 21), objectAt(0x80400000u, 51)});
  temporal::Census census{};
  const auto predecessors = temporal::pair(previous, current, census);

  CHECK_EQ((int)predecessors.size(), 3);
  CHECK_EQ((int)census.actors, 3);
  CHECK_EQ((int)census.interpolated, 3);
  CHECK_EQ((int)census.unpaired, 0);
  // The k-th draw of an object pairs with the k-th draw of that object before it, not with the
  // first one the previous update happened to contain.
  CHECK(predecessors[0] == &previous.objects[0]);
  CHECK(predecessors[1] == &previous.objects[2]);
  CHECK(predecessors[2] == &previous.objects[1]);
}

void test_an_object_the_previous_update_did_not_draw_is_unpaired(void) {
  const Input previous = frameOf({objectAt(0x80300000u, 10)});
  const Input current = frameOf({objectAt(0x80300000u, 11), objectAt(0x80500000u, 30)});
  temporal::Census census{};
  const auto predecessors = temporal::pair(previous, current, census);

  CHECK_EQ((int)predecessors.size(), 2);
  CHECK(predecessors[0] != nullptr);
  CHECK(predecessors[1] == nullptr);
  CHECK_EQ((int)census.interpolated, 1);
  CHECK_EQ((int)census.unpaired, 1);
  CHECK_EQ((int)census.incompatible, 0);

  // A second draw of an object the previous update drew only once is unpaired for the same reason.
  const Input twice = frameOf({objectAt(0x80300000u, 11), objectAt(0x80300000u, 21)});
  temporal::Census again{};
  const auto second = temporal::pair(previous, twice, again);
  CHECK(second[0] != nullptr);
  CHECK(second[1] == nullptr);
  CHECK_EQ((int)again.unpaired, 1);
}

void test_each_identity_rule_names_the_field_that_rejected_the_pair(void) {
  const Input previous =
      frameOf({objectAt(0x80300000u, 10, 3, 1), objectAt(0x80400000u, 50, 3, 1)});
  // One object's vertex corpus changed size and the other's face list did.
  const Input current = frameOf({objectAt(0x80300000u, 11, 4, 1), objectAt(0x80400000u, 51, 3, 2)});
  temporal::Census census{};
  const auto predecessors = temporal::pair(previous, current, census);

  CHECK(predecessors[0] == nullptr);
  CHECK(predecessors[1] == nullptr);
  CHECK_EQ((int)census.incompatible, 2);
  CHECK_EQ((int)census.interpolated, 0);
  CHECK_EQ((int)census.mismatches[(size_t)temporal::Mismatch::VertexCount], 1);
  CHECK_EQ((int)census.mismatches[(size_t)temporal::Mismatch::FaceCount], 1);
  CHECK_EQ((int)census.mismatches[(size_t)temporal::Mismatch::None], 0);
  CHECK_STREQ(temporal::mismatchName(temporal::Mismatch::VertexCount), "vertex-count");
  CHECK_STREQ(temporal::mismatchName(temporal::Mismatch::FaceCount), "face-count");

  // Directly, so a rule that only ever answered one way would be visible here.
  CHECK(temporal::mismatch(previous.objects[0], previous.objects[0]) == temporal::Mismatch::None);
  CHECK(temporal::mismatch(previous.objects[0], current.objects[0]) ==
        temporal::Mismatch::VertexCount);
  CHECK(temporal::mismatch(previous.objects[1], current.objects[1]) ==
        temporal::Mismatch::FaceCount);
}

void test_every_object_lands_in_exactly_one_census_bucket(void) {
  const Input previous = frameOf({objectAt(0x80300000u, 10), objectAt(0x80400000u, 50)});
  const Input current = frameOf(
      {objectAt(0x80300000u, 11), objectAt(0x80400000u, 51, 4, 1), objectAt(0x80500000u, 30)});
  temporal::Census census{};
  const auto predecessors = temporal::pair(previous, current, census);

  CHECK_EQ((int)predecessors.size(), 3);
  CHECK_EQ((int)census.actors, 3);
  CHECK_EQ((int)(census.interpolated + census.unpaired + census.incompatible + census.refused),
           (int)census.actors);
  CHECK_EQ((int)census.interpolated, 1);
  CHECK_EQ((int)census.incompatible, 1);
  CHECK_EQ((int)census.unpaired, 1);
  CHECK(census.worstMismatch() == temporal::Mismatch::VertexCount);
}

} // namespace

int main(void) {
  RUN(pairing_is_by_object_in_occurrence_order);
  RUN(an_object_the_previous_update_did_not_draw_is_unpaired);
  RUN(each_identity_rule_names_the_field_that_rejected_the_pair);
  RUN(every_object_lands_in_exactly_one_census_bucket);
  return pt_summary();
}
