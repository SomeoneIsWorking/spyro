#include "field_shaded_queue_recipe.h"
#include "testutil.h"

namespace {

spyro::field_shaded_queue_recipe::Input triangleInput() {
  using namespace spyro::field_shaded_queue_recipe;
  Input input{};
  input.projection = {.ofx = 256 << 16, .ofy = 120 << 16, .h = 341};
  input.colourMatrix = {{{{4096, 0, 0}}, {{0, 4096, 0}}, {{0, 0, 4096}}}};
  Record record{};
  record.actor = 0x80100000u;
  record.affine.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
  record.affine.t = {0, 0, 1000};
  record.lightBase = 0x00080808u;
  record.lightScale = 0x00ffffffu;
  record.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}};
  record.primitives = {{.indices = (1u << 16) | (2u << 9) | (2u << 2), .normal = 0x00010003u}};
  input.records.push_back(record);
  return input;
}

void test_shaded_triangle_preserves_depth_colour_and_authored_identity() {
  const auto recipe = spyro::field_shaded_queue_recipe::derive(triangleInput());
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.sourceRecords, 1u);
  CHECK_EQ(recipe.candidates, 1u);
  CHECK_EQ(recipe.rejected, 0u);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK_EQ(recipe.faces[0].vertexCount, 3u);
  CHECK_EQ(recipe.faces[0].otBin, 32u);
  CHECK_EQ(recipe.faces[0].rgb[0], 0x00080808u);
  CHECK(recipe.faces[0].semiTransparent);
  CHECK(!recipe.faces[0].gouraud);
  CHECK_EQ(recipe.faces[0].paintGroup, 0u);
}

void test_vertex_shaded_variant_uses_per_vertex_material_path() {
  auto input = triangleInput();
  input.records[0].primitives[0].normal = 0u;
  input.records[0].primitives[0].vertexColours = {
      0x00102030u, 0x00405060u, 0x00708090u, 0x00a0b0c0u};
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(recipe.faces[0].gouraud);
  CHECK(!recipe.faces[0].semiTransparent);
  CHECK_EQ(recipe.faces[0].rgb[0], 0x00102030u);
  CHECK_EQ(recipe.faces[0].rgb[1], 0x00405060u);
  CHECK_EQ(recipe.faces[0].rgb[2], 0x00708090u);
}

void test_mixed_variant_refuses_the_whole_recipe() {
  auto input = triangleInput();
  input.records[0].primitives.push_back(
      {.indices = (1u << 16) | (2u << 9) | (2u << 2), .normal = 0x00010001u});
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::UnsupportedVariant);
  CHECK_EQ(recipe.candidates, 2u);
  CHECK_EQ(recipe.firstUnsupportedActor, 0x80100000u);
  CHECK_EQ(recipe.firstUnsupportedPrimitive, 1u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

void test_common_clip_rejection_is_valid_empty() {
  auto input = triangleInput();
  input.records[0].clipMode = true;
  input.records[0].affine.t[0] = -4000;
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::ValidEmpty);
  CHECK_EQ(recipe.candidates, 0u);
  CHECK_EQ(recipe.rejected, 1u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

} // namespace

int main() {
  RUN(shaded_triangle_preserves_depth_colour_and_authored_identity);
  RUN(vertex_shaded_variant_uses_per_vertex_material_path);
  RUN(mixed_variant_refuses_the_whole_recipe);
  RUN(common_clip_rejection_is_valid_empty);
  return pt_summary();
}
