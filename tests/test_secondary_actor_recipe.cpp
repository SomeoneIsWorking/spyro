#include "secondary_actor_recipe.h"
#include "testutil.h"

namespace {

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
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(recipe.faces[0].family == spyro::actor_draw_recipe::Family::G3);
  CHECK_EQ(recipe.faces[0].input.color[0], 0x00112233u);
}

void test_live_specular_program_refuses_the_whole_call() {
  auto frame = frame_with(one_triangle(0u));
  spyro::secondary_actor_scene::Record spec{};
  spec.actor.expected = one_triangle(4u);
  frame.records.push_back(std::move(spec));
  const auto recipe = spyro::secondary_actor_recipe::derive(frame);
  CHECK(recipe.status == spyro::secondary_actor_recipe::Status::UnsupportedLighting);
  CHECK_EQ(recipe.firstUnsupportedRecord, 1u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

void test_culled_specular_candidate_needs_no_colour_program() {
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
  RUN(live_specular_program_refuses_the_whole_call);
  RUN(culled_specular_candidate_needs_no_colour_program);
  return pt_summary();
}
