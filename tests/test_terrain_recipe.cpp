// The terrain producer's recipe, tested at the four things only it owns: the transform its vertices
// go through, the interval between two game updates, the order in which retail's refusals fire, and
// the primitive-pool budget that decides where the picture stops.
//
// WHY THE REFUSAL ORDER IS TESTED AT ALL. Retail bounds-checks an object's face table and a face's
// colour words only after they survive clip rejection, so a capture that refused on either would
// refuse on objects and faces retail never looked at. That ordering is the whole reason those two
// conditions are carried into the corpus as flags instead of being refused where they are read, and
// a test that only drew a valid corpus would not notice if it were lost.
#include "terrain_recipe.h"

#include "testutil.h"

#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

using namespace spyro::terrain_recipe;
namespace np = psxport::native_projection;

constexpr np::ProjectionParams kProjection{.ofx = 256 << 16, .ofy = 120 << 16, .h = 341};
constexpr uint32_t kPoolBase = 0x80100000u;
constexpr uint32_t kFlatColour = 0x10112233u;

np::FixedAffine identityView() {
  return {.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}}, .t = {}};
}

// A small turn about Y, as the guest's own SHORTMATRIX would carry it. Small enough that the same
// geometry stays on screen through it, so a difference in the picture is the rotation and not a
// clip decision.
np::FixedAffine turnedView() {
  return {.m = {{{4076, 0, 400}, {0, 4096, 0}, {-400, 0, 4076}}}, .t = {}};
}

Object::Face triangle(uint32_t a, uint32_t b, uint32_t c, bool gouraud) {
  Object::Face face{};
  face.index = {a * 4u, b * 4u, c * 4u};
  face.source = 0x80200000u + a;
  face.gouraud = gouraud;
  face.rgb = gouraud ? std::array<uint32_t, 3>{0x00112233u, 0x00445566u, 0x00778899u}
                     : std::array<uint32_t, 3>{kFlatColour, kFlatColour, kFlatColour};
  return face;
}

// One triangle whose three model vertices already carry the object's world position, which is how
// the guest places terrain: the view matrix has no translation.
Object objectAt(uint32_t address, int16_t x) {
  Object object{};
  object.address = address;
  object.vertices = {{.x = x, .y = 0, .z = 1000},
                     {.x = (int16_t)(x + 100), .y = 0, .z = 1000},
                     {.x = x, .y = 100, .z = 1000}};
  object.faces = {triangle(0, 1, 2, false)};
  return object;
}

Input frameOf(std::initializer_list<Object> objects) {
  Input input{};
  input.view = identityView();
  input.projection = kProjection;
  input.rightClip = 512;
  input.poolCursor = kPoolBase;
  input.poolEnd = kPoolBase + 0x8000u;
  input.objects = objects;
  return input;
}

Interval intervalOver(const std::vector<const Object *> &previous,
                      const np::FixedAffine &previousView,
                      double t) {
  return {.previous = previous, .previousView = previousView, .t = t};
}

void test_an_object_is_transformed_by_the_update_s_own_view_matrix(void) {
  const Recipe recipe = derive(frameOf({objectAt(0x80300000u, 0)}));
  CHECK_STREQ(statusName(recipe.status), "Ready");
  CHECK_EQ((int)recipe.faces.size(), 1);
  CHECK_EQ((int)recipe.objects, 1);
  CHECK_EQ((int)recipe.vertices, 3);
  CHECK_EQ((int)recipe.f3, 1);
  CHECK_EQ((int)recipe.g3, 0);
  // The same pure projection the framework offers, with no ambient GTE state in between.
  const auto expected = np::project(identityView(), kProjection, {.x = 0, .y = 0, .z = 1000});
  CHECK_EQ((int)recipe.faces[0].vertices[0].sx, (int)expected.sx);
  CHECK_EQ((int)recipe.faces[0].vertices[0].sy, (int)expected.sy);
  CHECK(recipe.faces[0].vertices[0].screenX == expected.px);
  CHECK(recipe.faces[0].vertices[0].viewZ == expected.pz);
  CHECK_EQ(recipe.faces[0].object, 0x80300000u);
  // No interval was offered, so neither sampling counter may have moved.
  CHECK_EQ((int)recipe.sampled, 0);
  CHECK_EQ((int)recipe.sampleDeclined, 0);
}

void test_the_interval_reproduces_both_endpoints_and_moves_between_them(void) {
  const Input previous = frameOf({objectAt(0x80300000u, -200)});
  const Input current = frameOf({objectAt(0x80300000u, 200)});
  const std::vector<const Object *> paired{&previous.objects[0]};

  const Recipe ownPrevious = derive(previous);
  const Recipe ownCurrent = derive(current);
  const Interval start = intervalOver(paired, previous.view, 0.0);
  const Interval end = intervalOver(paired, previous.view, 1.0);
  const Interval middle = intervalOver(paired, previous.view, 0.5);
  const Recipe atStart = derive(current, &start);
  const Recipe atEnd = derive(current, &end);
  const Recipe atMiddle = derive(current, &middle);

  CHECK_EQ((int)ownPrevious.faces.size(), 1);
  CHECK_EQ((int)ownCurrent.faces.size(), 1);
  CHECK_EQ((int)atStart.faces.size(), 1);
  CHECK_EQ((int)atEnd.faces.size(), 1);
  CHECK_EQ((int)atMiddle.faces.size(), 1);
  CHECK_EQ((int)atStart.sampled, 1);
  CHECK_EQ((int)atStart.sampleDeclined, 0);
  CHECK_EQ((int)atMiddle.sampled, 1);
  // t=0 and t=1 are the two game updates' own pictures, exactly.
  CHECK_EQ((int)atStart.faces[0].vertices[0].sx, (int)ownPrevious.faces[0].vertices[0].sx);
  CHECK_EQ((int)atEnd.faces[0].vertices[0].sx, (int)ownCurrent.faces[0].vertices[0].sx);
  // and the half-way picture is strictly between them, which a recipe that ignored `t` could not
  // be.
  CHECK(atStart.faces[0].vertices[0].sx < atMiddle.faces[0].vertices[0].sx);
  CHECK(atMiddle.faces[0].vertices[0].sx < atEnd.faces[0].vertices[0].sx);
}

void test_the_view_matrix_is_sampled_alongside_the_vertices(void) {
  Input previous = frameOf({objectAt(0x80300000u, 300)});
  previous.view = turnedView();
  const Input current = frameOf({objectAt(0x80300000u, 300)});
  const std::vector<const Object *> paired{&previous.objects[0]};
  const Interval start = intervalOver(paired, previous.view, 0.0);
  const Interval middle = intervalOver(paired, previous.view, 0.5);

  const Recipe ownPrevious = derive(previous);
  CHECK_EQ((int)ownPrevious.faces.size(), 1);
  const Recipe atStart = derive(current, &start);
  const Recipe atMiddle = derive(current, &middle);
  const Recipe ownCurrent = derive(current);
  CHECK_EQ((int)atStart.faces.size(), 1);
  CHECK_EQ((int)atMiddle.faces.size(), 1);
  CHECK_EQ((int)ownCurrent.faces.size(), 1);
  // The two endpoints' vertices are identical here, so any motion at all can only have come from
  // sampling the view matrix.
  CHECK(ownPrevious.faces[0].vertices[0].sx != ownCurrent.faces[0].vertices[0].sx);
  CHECK_EQ((int)atStart.faces[0].vertices[0].sx, (int)ownPrevious.faces[0].vertices[0].sx);
  CHECK(atMiddle.faces[0].vertices[0].sx != ownPrevious.faces[0].vertices[0].sx);
  CHECK(atMiddle.faces[0].vertices[0].sx != ownCurrent.faces[0].vertices[0].sx);
}

void test_an_object_with_no_usable_predecessor_keeps_its_own_transform(void) {
  Input previous = frameOf({objectAt(0x80300000u, -200)});
  const Input current = frameOf({objectAt(0x80300000u, 200)});
  const Recipe ownCurrent = derive(current);
  CHECK_EQ((int)ownCurrent.faces.size(), 1);

  const std::vector<const Object *> absent{nullptr};
  const Interval unpaired = intervalOver(absent, previous.view, 0.5);
  const Recipe withoutEndpoint = derive(current, &unpaired);
  CHECK_EQ((int)withoutEndpoint.faces.size(), 1);
  CHECK_EQ((int)withoutEndpoint.faces[0].vertices[0].sx, (int)ownCurrent.faces[0].vertices[0].sx);
  CHECK_EQ((int)withoutEndpoint.sampled, 0);

  // A predecessor with a different mesh has no vertex correspondence to sample, so it is not a
  // predecessor at all.
  previous.objects[0].vertices.push_back({.x = 0, .y = 0, .z = 1000});
  const std::vector<const Object *> mismatched{&previous.objects[0]};
  const Interval reshaped = intervalOver(mismatched, previous.view, 0.5);
  const Recipe withReshapedEndpoint = derive(current, &reshaped);
  CHECK_EQ((int)withReshapedEndpoint.faces.size(), 1);
  CHECK_EQ((int)withReshapedEndpoint.faces[0].vertices[0].sx,
           (int)ownCurrent.faces[0].vertices[0].sx);
  CHECK_EQ((int)withReshapedEndpoint.sampled, 0);
}

void test_visibility_is_the_cull_matrix_depth_against_the_object_s_own_limit(void) {
  CHECK(visible(identityView(), {.x = 0, .y = 0, .z = 1000}, 500));
  CHECK(!visible(identityView(), {.x = 0, .y = 0, .z = 100}, 500));
  // The limit is the object's own, not a constant: the same point fails against a further one.
  CHECK(!visible(identityView(), {.x = 0, .y = 0, .z = 1000}, 2000));
}

void test_an_object_entirely_past_one_screen_edge_is_dropped_before_its_faces_are_read(void) {
  Input input = frameOf({objectAt(0x80300000u, 2000)});
  // Whatever is wrong with the face table cannot matter: retail never looks at it.
  input.objects[0].faceTableInRam = false;
  const Recipe recipe = derive(input);
  CHECK_STREQ(statusName(recipe.status), "ValidEmpty");
  CHECK_STREQ(recipe.refusal, "none");
  CHECK_EQ((int)recipe.objects, 1);
  CHECK_EQ((int)recipe.candidates, 0);

  // On screen, the same missing face table is a refusal.
  Input onScreen = frameOf({objectAt(0x80300000u, 0)});
  onScreen.objects[0].faceTableInRam = false;
  onScreen.objects[0].faces.clear();
  const Recipe refused = derive(onScreen);
  CHECK_STREQ(statusName(refused.status), "InvalidInput");
  CHECK_STREQ(refused.refusal, "face_span");
}

void test_a_face_past_one_edge_is_rejected_while_the_rest_of_the_object_draws(void) {
  Object object{};
  object.address = 0x80300000u;
  object.vertices = {{.x = -2000, .y = 0, .z = 1000},
                     {.x = -1900, .y = 0, .z = 1000},
                     {.x = -2000, .y = 100, .z = 1000},
                     {.x = 0, .y = 0, .z = 1000},
                     {.x = 100, .y = 0, .z = 1000},
                     {.x = 0, .y = 100, .z = 1000}};
  object.faces = {triangle(0, 1, 2, false), triangle(3, 4, 5, true)};
  // The rejected face's colour words are unreadable, and retail never reads them either.
  object.faces[0].colourInRam = false;
  const Recipe recipe = derive(frameOf({object}));
  CHECK_STREQ(statusName(recipe.status), "Ready");
  CHECK_EQ((int)recipe.candidates, 2);
  CHECK_EQ((int)recipe.rejects, 1);
  CHECK_EQ((int)recipe.faces.size(), 1);
  CHECK_EQ((int)recipe.f3, 0);
  CHECK_EQ((int)recipe.g3, 1);

  // Move the unreadable colour onto the face that survives and the same corpus refuses.
  Object refusing = object;
  refusing.faces[0].colourInRam = true;
  refusing.faces[1].colourInRam = false;
  const Recipe refused = derive(frameOf({refusing}));
  CHECK_STREQ(statusName(refused.status), "InvalidInput");
  CHECK_STREQ(refused.refusal, "color_index");
  CHECK_EQ((int)refused.faces.size(), 0);
}

void test_a_flat_face_loses_the_primitive_tag_a_gouraud_face_keeps(void) {
  Object object = objectAt(0x80300000u, 0);
  object.faces = {triangle(0, 1, 2, false), triangle(0, 1, 2, true)};
  const Recipe recipe = derive(frameOf({object}));
  CHECK_EQ((int)recipe.faces.size(), 2);
  CHECK_EQ((int)recipe.faces[0].rgb[0], (int)(kFlatColour - 0x10000000u));
  CHECK_EQ((int)recipe.faces[1].rgb[0], 0x00112233);
  CHECK_EQ((int)recipe.faces[1].rgb[1], 0x00445566);
}

void test_the_picture_stops_where_the_guest_s_primitive_pool_would_have_run_out(void) {
  Object object = objectAt(0x80300000u, 0);
  object.faces = {triangle(0, 1, 2, false), triangle(0, 1, 2, false), triangle(0, 1, 2, false)};
  // Room for two flat triangles at twenty bytes each, and not the third.
  Input input = frameOf({object});
  input.poolEnd = input.poolCursor + 40u;
  const Recipe recipe = derive(input);
  CHECK_STREQ(statusName(recipe.status), "PoolExhausted");
  CHECK_STREQ(recipe.refusal, "pool_exhaustion_equivalent");

  // One byte more of pool and the whole object draws, so the budget is what stopped it.
  input.poolEnd = input.poolCursor + 41u;
  const Recipe complete = derive(input);
  CHECK_STREQ(statusName(complete.status), "Ready");
  CHECK_EQ((int)complete.faces.size(), 3);
}

void test_a_face_naming_a_vertex_the_object_does_not_own_refuses(void) {
  Object misaligned = objectAt(0x80300000u, 0);
  misaligned.faces[0].index[1] = 5u;
  const Recipe unaligned = derive(frameOf({misaligned}));
  CHECK_STREQ(statusName(unaligned.status), "InvalidInput");
  CHECK_STREQ(unaligned.refusal, "vertex_index_alignment");

  Object outOfRange = objectAt(0x80300000u, 0);
  outOfRange.faces[0].index[2] = 40u;
  const Recipe unowned = derive(frameOf({outOfRange}));
  CHECK_STREQ(statusName(unowned.status), "InvalidInput");
  CHECK_STREQ(unowned.refusal, "external_vertex_source_unowned");
}

} // namespace

int main(void) {
  RUN(an_object_is_transformed_by_the_update_s_own_view_matrix);
  RUN(the_interval_reproduces_both_endpoints_and_moves_between_them);
  RUN(the_view_matrix_is_sampled_alongside_the_vertices);
  RUN(an_object_with_no_usable_predecessor_keeps_its_own_transform);
  RUN(visibility_is_the_cull_matrix_depth_against_the_object_s_own_limit);
  RUN(an_object_entirely_past_one_screen_edge_is_dropped_before_its_faces_are_read);
  RUN(a_face_past_one_edge_is_rejected_while_the_rest_of_the_object_draws);
  RUN(a_flat_face_loses_the_primitive_tag_a_gouraud_face_keeps);
  RUN(the_picture_stops_where_the_guest_s_primitive_pool_would_have_run_out);
  RUN(a_face_naming_a_vertex_the_object_does_not_own_refuses);
  return pt_summary();
}
