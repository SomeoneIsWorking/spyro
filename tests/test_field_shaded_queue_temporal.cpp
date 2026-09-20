// The world-shaded sprite queue's temporal source, tested at the two things only it owns: which of
// the previous frame's records is this record, and what the interval does to the picture.
//
// The endpoint lifecycle is `spyro::temporal::Pair` and the occurrence-ordered walk is
// `spyro::instance_pairing`; both are exercised where they live. What is tested here is this
// layer's identity rule and the interval reaching the projection at all.
#include "field_shaded_queue_temporal.h"

#include "testutil.h"

#include <algorithm>
#include <string_view>

namespace {

using namespace spyro::field_shaded_queue_recipe;
namespace temporal = spyro::field_shaded_queue_temporal;

Record triangleAt(uint32_t actor, int32_t tx) {
  Record record{};
  record.actor = actor;
  record.affine.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
  record.affine.t = {tx, 0, 1000};
  record.lightBase = 0x00080808u;
  record.lightScale = 0x00ffffffu;
  record.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
  record.primitives = {{.indices = (1u << 16) | (2u << 9) | (2u << 2) | 3u, .normal = 0x00010000u}};
  return record;
}

Input frameOf(std::initializer_list<std::pair<uint32_t, int32_t>> draws) {
  Input input{};
  input.projection = {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341};
  input.colourMatrix = {{{{4096, 0, 0}}, {{0, 4096, 0}}, {{0, 0, 4096}}}};
  for (const auto &[actor, tx] : draws) {
    input.records.push_back(triangleAt(actor, tx));
  }
  return input;
}

Recipe sampleAt(const Input &previous, const Input &current, double t, temporal::Census &census) {
  const std::vector<const Record *> predecessors = temporal::pair(previous, current, census);
  const Interval interval{.previous = std::span<const Record *const>(predecessors), .t = t};
  return derive(current, &interval);
}

// The discriminator for the interval reaching the projection. A source that paired correctly and
// then ignored the predecessor would still count every record and still reproduce the current
// frame; it would not move the picture at the midpoint.
void test_the_interval_reproduces_both_endpoints_and_moves_between_them() {
  const auto previous = frameOf({{0x80100000u, 0}});
  const auto current = frameOf({{0x80100000u, 1024}});
  const auto own = derive(current);
  const auto was = derive(previous);
  CHECK(own.status == Status::Ready && was.status == Status::Ready);
  CHECK(own.faces[0].vertices[0].sx != was.faces[0].vertices[0].sx);
  CHECK_EQ(own.sampled, 0u);
  CHECK_EQ(own.sampleDeclined, 0u);

  temporal::Census census{};
  const auto atOne = sampleAt(previous, current, 1.0, census);
  CHECK(atOne.status == Status::Ready);
  CHECK_EQ(census.actors, 1u);
  CHECK_EQ(census.interpolated, 1u);
  CHECK_EQ(atOne.sampled, 1u);
  CHECK_EQ(atOne.sampleDeclined, 0u);
  CHECK_EQ(atOne.faces[0].vertices[0].sx, own.faces[0].vertices[0].sx);
  CHECK_EQ(atOne.faces[0].vertices[0].sy, own.faces[0].vertices[0].sy);
  CHECK_EQ(atOne.faces[0].otBin, own.faces[0].otBin);
  CHECK_EQ(atOne.faces[0].rgb[0], own.faces[0].rgb[0]);

  const auto atZero = sampleAt(previous, current, 0.0, census);
  CHECK_EQ(atZero.faces[0].vertices[0].sx, was.faces[0].vertices[0].sx);
  CHECK_EQ(atZero.faces[0].vertices[0].sy, was.faces[0].vertices[0].sy);

  const auto atHalf = sampleAt(previous, current, 0.5, census);
  const int32_t low = std::min(was.faces[0].vertices[0].sx, own.faces[0].vertices[0].sx);
  const int32_t high = std::max(was.faces[0].vertices[0].sx, own.faces[0].vertices[0].sx);
  CHECK(atHalf.faces[0].vertices[0].sx > low);
  CHECK(atHalf.faces[0].vertices[0].sx < high);
}

// The cursor has to advance on every draw, so the second draw of one actor pairs with the second
// draw of that actor and not with the first.
void test_pairing_is_by_actor_in_occurrence_order() {
  const auto previous = frameOf({{0x80100000u, 0}, {0x80100040u, 100}, {0x80100000u, 200}});
  const auto current = frameOf({{0x80100000u, 10}, {0x80100000u, 210}, {0x80100040u, 110}});
  temporal::Census census{};
  const auto predecessors = temporal::pair(previous, current, census);

  CHECK_EQ(predecessors.size(), 3u);
  CHECK_EQ(census.actors, 3u);
  CHECK_EQ(census.interpolated, 3u);
  CHECK_EQ(census.unpaired(), 0u);
  CHECK(predecessors[0] == &previous.records[0]);
  CHECK(predecessors[1] == &previous.records[2]);
  CHECK(predecessors[2] == &previous.records[1]);
}

void test_an_unattributed_or_absent_draw_is_unpaired_and_keeps_its_own_transform() {
  const auto previous = frameOf({{0x80100000u, 0}});
  auto current = frameOf({{0x80100000u, 1024}, {0x80100040u, 512}, {0u, 256}});
  temporal::Census census{};
  const auto sampled = sampleAt(previous, current, 0.0, census);

  CHECK_EQ(census.actors, 3u);
  CHECK_EQ(census.interpolated, 1u);
  // The two draws fail for DIFFERENT reasons and the census must say which is which: 0x80100040
  // was attributed and the previous frame simply did not draw it, while the third draw carries no
  // instance at all and could never pair with anything. One merged count cannot tell a real spawn
  // from a producer that failed to attribute a draw, and they have different owners.
  CHECK_EQ(census.absent, 1u);
  CHECK_EQ(census.unattributed, 1u);
  // And it must NAME the absent one. A count says an object was shown a whole frame early; only
  // the identity distinguishes the same instance failing every frame from a different one each
  // time, which is the difference between a defect and an object entering the scene.
  CHECK_EQ(census.absentInstances[0], 0x80100040u);
  CHECK_EQ(census.unpaired(), 2u);
  CHECK_EQ(census.incompatible, 0u);
  CHECK_EQ(census.interpolated + census.unpaired() + census.incompatible + census.refused,
           census.actors);
  CHECK_EQ(sampled.sampled, 1u);
  CHECK_EQ(sampled.sampleDeclined, 0u);
  // An unpaired record is shown where its own frame put it, not where the paired one was pulled to.
  const auto own = derive(current);
  CHECK_EQ(sampled.faces[1].vertices[0].sx, own.faces[1].vertices[0].sx);
  CHECK_EQ(sampled.faces[2].vertices[0].sx, own.faces[2].vertices[0].sx);
}

// A bare "not interpolated" count cannot be told from a rule that never ran, so each rejection
// names the field that differed.
void test_each_identity_rule_names_the_field_that_rejected_the_pair() {
  const auto previous = frameOf({{0x80100000u, 0}});

  auto mesh = frameOf({{0x80100000u, 1024}});
  mesh.records[0].meshIndex = 7;
  temporal::Census census{};
  auto sampled = sampleAt(previous, mesh, 0.0, census);
  CHECK_EQ(census.incompatible, 1u);
  CHECK_EQ(census.interpolated, 0u);
  CHECK(census.worstMismatch() == temporal::Mismatch::MeshIndex);
  CHECK_EQ(sampled.sampled, 0u);
  // The rejected record is shown at its own endpoint, not at the predecessor's.
  CHECK_EQ(sampled.faces[0].vertices[0].sx, derive(mesh).faces[0].vertices[0].sx);

  auto vertices = frameOf({{0x80100000u, 1024}});
  vertices.records[0].vertices.push_back({50, 50, 0});
  sampleAt(previous, vertices, 0.0, census);
  CHECK(census.worstMismatch() == temporal::Mismatch::VertexCount);

  auto primitives = frameOf({{0x80100000u, 1024}});
  primitives.records[0].primitives.push_back(primitives.records[0].primitives[0]);
  sampleAt(previous, primitives, 0.0, census);
  CHECK(census.worstMismatch() == temporal::Mismatch::PrimitiveCount);

  auto clip = frameOf({{0x80100000u, 1024}});
  clip.records[0].clipMode = true;
  sampleAt(previous, clip, 0.0, census);
  CHECK(census.worstMismatch() == temporal::Mismatch::ClipMode);
  CHECK(std::string_view(temporal::mismatchName(temporal::Mismatch::ClipMode)) == "clip-mode");

  // And the compatible case stays out of the breakdown entirely.
  sampleAt(previous, frameOf({{0x80100000u, 1024}}), 0.0, census);
  CHECK_EQ(census.incompatible, 0u);
  CHECK(census.worstMismatch() == temporal::Mismatch::None);
}

} // namespace

int main() {
  RUN(the_interval_reproduces_both_endpoints_and_moves_between_them);
  RUN(pairing_is_by_actor_in_occurrence_order);
  RUN(an_unattributed_or_absent_draw_is_unpaired_and_keeps_its_own_transform);
  RUN(each_identity_rule_names_the_field_that_rejected_the_pair);
  return pt_summary();
}
