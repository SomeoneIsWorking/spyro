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
  record.primitives = {{.indices = (1u << 16) | (2u << 9) | (2u << 2) | 3u, .normal = 0x00010000u}};
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
  CHECK_EQ(recipe.faces[0].actor, 0x80100000u);
  CHECK_EQ(recipe.faces[0].vertexCount, 3u);
  CHECK_EQ(recipe.faces[0].otBin, 32u);
  CHECK_EQ(recipe.faces[0].rgb[0], 0x00080808u);
  CHECK_EQ(recipe.faces[0].vertices[0].viewZ, 1000.0f);
  CHECK(recipe.faces[0].semiTransparent);
  CHECK(!recipe.faces[0].gouraud);
  CHECK_EQ(recipe.faces[0].paintGroup, 0u);
}

void test_vertex_shaded_variant_uses_per_vertex_material_path() {
  auto input = triangleInput();
  input.records[0].primitives[0].indices = (1u << 16) | (2u << 9) | (2u << 2);
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

void test_one_bad_primitive_refuses_the_whole_recipe() {
  auto input = triangleInput();
  // An index past the projected list is the remaining atomic refusal: one bad primitive must void
  // the recipe rather than emit the good one. (Variant 1, which used to serve this purpose, is now
  // a supported lit arm.)
  input.records[0].primitives.push_back(
      {.indices = (9u << 16) | (2u << 9) | (2u << 2) | 3u, .normal = 0x00010000u});
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::InvalidInput);
  CHECK_EQ(recipe.candidates, 2u);
  CHECK_EQ(recipe.firstUnsupportedActor, 0x80100000u);
  CHECK_EQ(recipe.firstUnsupportedPrimitive, 1u);
  CHECK_EQ(recipe.faces.size(), 0u);
}

// r_moby.s reads the primitive word's bit 1 only inside the bit-0 path, so a primitive carrying bit
// 1 and NOT bit 0 is the same code as one carrying neither: per-vertex Gouraud. Refusing it was
// refusing a combination retail cannot distinguish, and it is the flag half of the variant-1
// family.
void test_high_variant_bit_alone_is_the_per_vertex_path() {
  auto input = triangleInput();
  input.records[0].primitives[0].indices = (1u << 16) | (2u << 9) | (2u << 2) | 2u;
  input.records[0].primitives[0].normal = 0u;
  input.records[0].primitives[0].vertexColours = {
      0x00102030u, 0x00405060u, 0x00708090u, 0x00a0b0c0u};
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(recipe.faces[0].gouraud);
  CHECK(!recipe.faces[0].semiTransparent);
  CHECK_EQ(recipe.faces[0].rgb[1], 0x00405060u);
}

// The lit path with bit 1 clear is variant 1 (.L80023534's fall-through): a flat face whose colour
// comes from the SINGLE entry word, not the base/scale pair. Expected colour derived from the arm's
// own steps: channels = ((e<<4)&0xFF0, (e>>4)&0xFF0, (e>>12)&0xFF0) = (0x080, 0x080, 0x880); scale
// = (e>>23)&0x1E = 30; the colour word 0x00010000 gives the vector (1, 0, 0); no shift, so linear =
// (128 + 30, 128, 2176) -> >>4 = (9, 8, 136) = 0x880809. lightEntryIndex 0 is what arms the
// semi-transparency bit on this path (r_moby.s 0x8002363C: `bnez $t7` skips it).
void test_lit_path_without_the_high_bit_uses_the_single_entry() {
  auto input = triangleInput();
  input.records[0].lightEntry = 0x0f880808u;
  input.records[0].lightEntryIndex = 0;
  input.records[0].primitives[0].indices = (1u << 16) | (2u << 9) | (2u << 2) | 1u;
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(!recipe.faces[0].gouraud);
  CHECK_EQ(recipe.faces[0].rgb[0], 0x00880809u);
  CHECK(recipe.faces[0].semiTransparent);
}

// A non-zero entry index means the arm does NOT set the semi-transparency bit.
void test_variant_one_is_opaque_away_from_index_zero() {
  auto input = triangleInput();
  input.records[0].lightEntry = 0x0f880808u;
  input.records[0].lightEntryIndex = 4;
  input.records[0].primitives[0].indices = (1u << 16) | (2u << 9) | (2u << 2) | 1u;
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK(!recipe.faces[0].semiTransparent);
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

void test_flat_arm_semi_transparency_follows_the_near_camera_branch() {
  auto nearInput = triangleInput();
  auto farInput = triangleInput();
  farInput.records[0].affine.t[2] = 4000;
  const auto nearRecipe = spyro::field_shaded_queue_recipe::derive(nearInput);
  const auto farRecipe = spyro::field_shaded_queue_recipe::derive(farInput);
  CHECK(nearRecipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK(farRecipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK(nearRecipe.faces[0].semiTransparent);
  CHECK(!farRecipe.faces[0].semiTransparent);
  CHECK_EQ(nearRecipe.faces[0].rgb[0], farRecipe.faces[0].rgb[0]);
}

// The flat arm's colour is the selected light entry (the D_8006E44C base/scale pair) alone; the
// source's light-table byte offset chooses that entry and never biases the colour command.
void test_flat_arm_colour_is_the_selected_light_entry() {
  auto input = triangleInput();
  input.records[0].lightBase = 0x00102030u;
  input.records[0].lightScale = 0x00808080u;
  const auto recipe = spyro::field_shaded_queue_recipe::derive(input);
  CHECK(recipe.status == spyro::field_shaded_queue_recipe::Status::Ready);
  CHECK_EQ(recipe.faces.size(), 1u);
  CHECK_EQ(recipe.faces[0].rgb[0], 0x00102030u);
}

} // namespace

int main() {
  RUN(shaded_triangle_preserves_depth_colour_and_authored_identity);
  RUN(vertex_shaded_variant_uses_per_vertex_material_path);
  RUN(one_bad_primitive_refuses_the_whole_recipe);
  RUN(high_variant_bit_alone_is_the_per_vertex_path);
  RUN(lit_path_without_the_high_bit_uses_the_single_entry);
  RUN(variant_one_is_opaque_away_from_index_zero);
  RUN(common_clip_rejection_is_valid_empty);
  RUN(flat_arm_semi_transparency_follows_the_near_camera_branch);
  RUN(flat_arm_colour_is_the_selected_light_entry);
  return pt_summary();
}
