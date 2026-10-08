// The Moby scale byte at +0x57. Both retail actor renderers (0x80022A2C at .L80022CCC and
// 0x8001F798 at 0x8001F868) end their transform setup by running the doubled view translation
// through GPF with the byte in IR0 and reading MAC back with `sra 5`. The port refused any actor
// carrying a nonzero byte, which is what stopped the dragon cutscene the moment its "Rescued ..."
// text mobys entered the shaded queue.
#include "actor_transform_math.h"
#include "testutil.h"

namespace {

using spyro::actor_transform_math::scaledTranslation;

void test_a_zero_byte_leaves_the_translation_alone() {
  // Retail branches around the multiply. Treating zero as a scale factor would collapse every
  // unscaled actor onto the camera, which is the failure mode this case exists to exclude.
  const std::array<int32_t, 3> doubled{100, -200, 3000};
  const auto out = scaledTranslation(doubled, 0);
  CHECK_EQ(out[0], 100);
  CHECK_EQ(out[1], -200);
  CHECK_EQ(out[2], 3000);
}

void test_thirty_two_is_the_identity_factor() {
  const auto out = scaledTranslation({64, -128, 1024}, 32);
  CHECK_EQ(out[0], 64);
  CHECK_EQ(out[1], -128);
  CHECK_EQ(out[2], 1024);
}

void test_a_smaller_byte_pulls_the_actor_in() {
  const auto out = scaledTranslation({640, -640, 1280}, 16);
  CHECK_EQ(out[0], 320);
  CHECK_EQ(out[1], -320);
  CHECK_EQ(out[2], 640);
}

void test_the_shift_is_arithmetic_not_a_division() {
  // -1 * 32 >> 5 is -1 under an arithmetic shift and 0 under truncating division. The guest shifts.
  const auto out = scaledTranslation({-1, -31, -33}, 1);
  CHECK_EQ(out[0], -1);
  CHECK_EQ(out[1], -1);
  CHECK_EQ(out[2], -2);
}

void test_the_input_wraps_at_sixteen_bits_like_the_ir_registers() {
  // mtc2 into IR1..IR3 keeps the low 16 bits. 0x8000 is -32768 there, not +32768, so a widened
  // implementation would put a far actor on the opposite side of the camera from the guest.
  const auto wrapped = scaledTranslation({0x8000, 0x10000, 0x1000}, 32);
  CHECK_EQ(wrapped[0], -32768);
  CHECK_EQ(wrapped[1], 0);
  CHECK_EQ(wrapped[2], 0x1000);
}

} // namespace

int main() {
  RUN(a_zero_byte_leaves_the_translation_alone);
  RUN(thirty_two_is_the_identity_factor);
  RUN(a_smaller_byte_pulls_the_actor_in);
  RUN(the_shift_is_arithmetic_not_a_division);
  RUN(the_input_wraps_at_sixteen_bits_like_the_ir_registers);
  return pt_summary();
}
