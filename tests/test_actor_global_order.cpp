#include "actor_global_order.h"
#include "actor_ot_coalescer.h"
#include "paired_actor_depth.h"
#include "testutil.h"

#include <algorithm>

namespace {

spyro::actor_draw_recipe::Face face(uint32_t record, uint32_t localBin, uint32_t ordinal) {
  spyro::actor_draw_recipe::Face out{};
  out.record = record;
  out.localBin = localBin;
  out.sourceOrdinal = ordinal;
  return out;
}

void test_near_clamp_preserves_local_fifo() {
  spyro::actor_prefix::Output record{};
  record.controls[13] = 0;
  record.controls[14] = 0;
  record.depthOrigin = 1;
  record.otShift = 4;
  const std::vector<spyro::actor_prefix::Output> records{record};
  const std::vector<spyro::actor_draw_recipe::Face> faces{
      face(0, 40, 0), face(0, 40, 1), face(0, 39, 2), face(0, 0, 3)};
  const auto result = spyro::actor_global_order::build(records, faces);
  CHECK_EQ((int)result.status, (int)spyro::actor_global_order::Status::Ready);
  CHECK_EQ(result.faces.size(), faces.size());
  CHECK_EQ(result.faces[0].faceIndex, 0);
  CHECK_EQ(result.faces[0].otBin, 0);
  CHECK_EQ(result.faces[0].chainOrdinal, 0);
  CHECK_EQ(result.faces[1].faceIndex, 1);
  CHECK_EQ(result.faces[1].otBin, 0);
  CHECK_EQ(result.faces[1].chainOrdinal, 1);
  CHECK_EQ(result.faces[2].faceIndex, 2);
  CHECK_EQ(result.faces[2].otBin, 0);
  CHECK_EQ(result.faces[2].chainOrdinal, 2);
  CHECK_EQ(result.faces[3].faceIndex, 3);
  CHECK_EQ(result.faces[3].otBin, 0);
  CHECK_EQ(result.faces[3].chainOrdinal, 3);
}

void test_record_append_and_negative_controls() {
  spyro::actor_prefix::Output record{};
  record.controls[14] = 12;
  record.depthOrigin = 1;
  record.otShift = 4;
  const std::vector<spyro::actor_prefix::Output> records{record, record};
  const std::vector<spyro::actor_draw_recipe::Face> faces{
      face(0, 4, 1), face(0, 4, 0), face(1, 4, 0)};
  const auto result = spyro::actor_global_order::build(records, faces);
  CHECK_EQ((int)result.status, (int)spyro::actor_global_order::Status::Ready);
  CHECK_EQ(result.faces[0].faceIndex, 1);
  CHECK_EQ(result.faces[1].faceIndex, 0);
  CHECK_EQ(result.faces[0].recordOrdinal, 0);
  CHECK_EQ(result.faces[2].recordOrdinal, 1);

  auto badFaces = faces;
  badFaces[0].localBin = 288;
  const auto badBin = spyro::actor_global_order::build(records, badFaces);
  CHECK_EQ((int)badBin.status, (int)spyro::actor_global_order::Status::InvalidLocalBin);
  badFaces = faces;
  badFaces[0].record = 2;
  const auto badRecord = spyro::actor_global_order::build(records, badFaces);
  CHECK_EQ((int)badRecord.status, (int)spyro::actor_global_order::Status::InvalidRecord);
}

void test_chunk_boundaries_holes_and_controls() {
  // Control 1 consumes 16 local buckets per global slot. Empty buckets consume their place.
  const std::vector<uint32_t> bins{130, 115, 114, 99, 98, 19, 18, 3, 2};
  const auto mapped = spyro::actor_ot_coalescer::map({0, 7, 1}, bins);
  CHECK(mapped.valid);
  CHECK(mapped.bins == std::vector<uint16_t>({7, 7, 6, 6, 5, 1, 0, 0, 0}));
  const auto shifted = spyro::actor_ot_coalescer::map({128, 7, 1}, bins);
  CHECK(shifted.valid);
  CHECK(shifted.bins == std::vector<uint16_t>({8, 8, 7, 7, 6, 2, 1, 1, 0}));
  const std::array<uint32_t, 2> tail{287, 271};
  const auto end = spyro::actor_ot_coalescer::map({0, 7, 1}, tail);
  CHECK(end.valid);
  CHECK(end.bins == std::vector<uint16_t>({14, 13}));
  CHECK(spyro::actor_ot_coalescer::map({0, 7, 1}, {}).valid);
  CHECK(!spyro::actor_ot_coalescer::map({0, 7, 6}, bins).valid);
  CHECK(!spyro::actor_ot_coalescer::map({1, 7, 1}, bins).valid);
  CHECK(!spyro::actor_ot_coalescer::map({0, 2048, 1}, tail).valid);
  CHECK(!spyro::actor_ot_coalescer::map({0, 7, 1}, std::array<uint32_t, 1>{288}).valid);
}

void test_source_depth_derivation() {
  using namespace spyro::paired_actor_depth;
  const auto near = derive(1023, 7, 1);
  CHECK_EQ(near.origin, 0.0);
  CHECK_EQ(near.near, 0u);
  CHECK_EQ(near.shift, 5u);
  const auto far = derive(1152, 7, 1);
  CHECK_EQ(far.origin, 128.0);
  CHECK_EQ(far.near, 2u);
  const auto mid = interpolate(1023, 1152, 7, 1, 0.5f);
  CHECK(mid.has_value());
  CHECK_EQ(mid->origin, 63.5);
  CHECK_EQ(mid->near, 1u);
  CHECK_EQ(derive(-1, 0, 0).near, 0u);
  CHECK_EQ(derive(INT32_MIN, 0, 0).origin, 2147483136.0);
  CHECK(!interpolate(0, 100, 0, 0, -1).has_value());
  CHECK(!interpolate(0, 100, 0, 0, 2).has_value());
}

} // namespace

int main() {
  RUN(near_clamp_preserves_local_fifo);
  RUN(record_append_and_negative_controls);
  RUN(chunk_boundaries_holes_and_controls);
  RUN(source_depth_derivation);
  return pt_summary();
}
