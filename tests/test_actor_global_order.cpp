#include "actor_global_order.h"
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

void test_observed_base_bounce_fixture() {
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
  CHECK_EQ(result.faces[3].otBin, 1);
  CHECK_EQ(result.faces[3].chainOrdinal, 0);
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

} // namespace

int main() {
  RUN(observed_base_bounce_fixture);
  RUN(record_append_and_negative_controls);
  return pt_summary();
}
