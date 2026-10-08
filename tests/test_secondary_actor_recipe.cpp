#include "secondary_actor_recipe.h"
#include "testutil.h"

#include <cmath>

namespace {

// 0x80074B84 lives in the player's own executable, so this stand-in is shaped the way the
// normalise reads it — entry i answers for a mantissa of i + 0x40 — rather than copied from it.
// It proves the program runs end to end and that every vertex ends on one colour; agreement with
// retail's own table is the oracle's job, not this test's.
struct MagnitudeStandIn {
  std::array<std::int16_t, spyro::face_light::kMagnitudeEntries> entries{};

  MagnitudeStandIn() {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      entries[i] = (std::int16_t)std::lround(std::sqrt((double)(i + 0x40)) * 362.0);
    }
  }

  spyro::face_light::Environment environment() const {
    return {.light = {0x1000, 0x0800, 0x0400}, .magnitude = entries};
  }
};

spyro::actor_prefix::Output one_triangle(uint32_t control, bool visible = true) {
  spyro::actor_prefix::Output out{};
  out.status = spyro::actor_prefix::Status::Ok;
  out.controls[13] = 2u;
  out.controls[14] = 8u;
  out.otShift = 6u;
  out.vertices.resize(3);
  out.colors = {0x00112233u};
  out.primitiveWords = {control | 0x00002020u, 0u};
  const int16_t x[3] = {0, 10, 0};
  const int16_t y[3] = {0, 0, 10};
  for (uint32_t i = 0; i < 3; ++i) {
    out.vertices[i].scratchWord = 0u;
    out.vertices[i].projected.sx = x[i];
    out.vertices[i].projected.sy = y[i];
    out.vertices[i].projected.sz = visible ? 64u : 0u;
    out.vertices[i].projected.px = (float)x[i];
    out.vertices[i].projected.py = (float)y[i];
    out.vertices[i].projected.pz = visible ? 64.0f : 0.0f;
    // The colour program differences these, so a face tilted out of the view plane is what gives
    // it a normal to work from at all.
    out.vertices[i].projected.raw_view_fixed = {
        (int64_t)x[i] << 12, (int64_t)y[i] << 12, (int64_t)(64 + i * 3) << 12};
  }
  return out;
}

spyro::secondary_actor_scene::Frame frame_with(spyro::actor_prefix::Output output) {
  spyro::secondary_actor_scene::Frame frame{};
  spyro::secondary_actor_scene::Record record{};
  record.actor.expected = std::move(output);
  frame.records.push_back(std::move(record));
  return frame;
}

void test_empty_source_list_is_valid_empty() {
  const auto recipe = spyro::secondary_actor_recipe::derive(spyro::secondary_actor_scene::Frame{});
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::ValidEmpty);
  CHECK_EQ(recipe.sourceRecords, 0u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

void test_ordinary_colour_triangle_keeps_native_topology() {
  const auto recipe = spyro::secondary_actor_recipe::derive(frame_with(one_triangle(0u)));
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::Ready);
  CHECK_EQ(recipe.candidates, 1u);
  CHECK_EQ(recipe.faceLightFaces, 0u);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(recipe.faces[0].family == spyro::actor_draw_recipe::Family::G3);
  CHECK_EQ(recipe.faces[0].input.color[0], 0x00112233u);
}

void test_face_light_triangle_without_a_table_refuses_the_whole_call() {
  auto frame = frame_with(one_triangle(0u));
  spyro::secondary_actor_scene::Record lit{};
  lit.actor.expected = one_triangle(4u);
  lit.lightingControl = 0x00123456u;
  frame.records.push_back(std::move(lit));
  const auto recipe = spyro::secondary_actor_recipe::derive(frame);
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::UnsupportedTopology);
  CHECK(recipe.firstReason == spyro::actor_draw_recipe::Reason::FaceLight);
  CHECK(recipe.firstLightingStatus == spyro::face_light::Status::NoEnvironment);
  CHECK_EQ(recipe.firstUnsupportedRecord, 1u);
  CHECK_EQ(recipe.firstUnsupportedLighting, 0x00123456u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

void test_face_light_triangle_gives_every_vertex_one_computed_colour() {
  const MagnitudeStandIn table;
  auto frame = frame_with(one_triangle(4u));
  frame.records[0].lightingControl = 0x00123456u;
  const auto recipe = spyro::secondary_actor_recipe::derive(frame, table.environment());
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  const auto &input = recipe.faces[0].input;
  CHECK(input.color[0] == input.color[1] && input.color[1] == input.color[2]);
  CHECK(input.color[0] != 0x00112233u);
  CHECK_EQ(input.color[0] & 0xff000000u, 0u);
  CHECK_EQ(recipe.faceLightFaces, 1u);
}

void test_tint_program_draws_and_makes_the_face_opaque() {
  const MagnitudeStandIn table;
  auto frame = frame_with(one_triangle(4u));
  frame.records[0].lightingControl = 0x01123456u; // a non-zero top byte selects 0x80021FE0
  const auto recipe = spyro::secondary_actor_recipe::derive(frame, table.environment());
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  const auto &input = recipe.faces[0].input;
  // The tint arm keeps three separate colours, unlike the directional arm above which flattens
  // them, and it stores the command byte itself so the face is opaque.
  CHECK(input.opaqueCommand);
  CHECK(input.color[0] != 0x00112233u);
}

void test_culled_face_light_candidate_needs_no_colour_program() {
  auto output = one_triangle(4u);
  output.vertices[2].projected.sy = 0;
  const auto recipe = spyro::secondary_actor_recipe::derive(frame_with(std::move(output)));
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::ValidEmpty);
  CHECK_EQ(recipe.rejectedCandidates, 1u);
}

} // namespace

int main() {
  RUN(empty_source_list_is_valid_empty);
  RUN(ordinary_colour_triangle_keeps_native_topology);
  RUN(face_light_triangle_without_a_table_refuses_the_whole_call);
  RUN(face_light_triangle_gives_every_vertex_one_computed_colour);
  RUN(tint_program_draws_and_makes_the_face_opaque);
  RUN(culled_face_light_candidate_needs_no_colour_program);
  return pt_summary();
}
