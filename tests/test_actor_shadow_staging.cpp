// The two `bgez` branches at 0x8001F344/0x8001F350 decide whether a Moby joins the shadow list the
// moby-shadow producer 0x80059F8C later consumes. Both branches skip, and the depth they compare is
// positive, so a mis-signed limit turns the pair into a contradiction that stages nothing. That is
// exactly what shipped: every Moby shadow in the game was silently absent while the producer
// reported a healthy empty list. These cases pin both answers.
#include "actor_scene_builder.h"
#include "testutil.h"

using spyro::actor_scene::classify_view;
using spyro::actor_scene::kShadedShadowStagingDepth;
using spyro::actor_scene::kShadowStagingDepth;
using spyro::actor_scene::stages_shadow;

namespace {

void test_near_moby_with_a_shadow_stages() {
  CHECK(stages_shadow(-1, 0, kShadowStagingDepth));
  CHECK(stages_shadow((int32_t)0x8FC0085C, kShadowStagingDepth - 1, kShadowStagingDepth));
}

void test_shadowless_moby_never_stages() {
  CHECK(!stages_shadow(0, 0, kShadowStagingDepth));
  CHECK(!stages_shadow(0x7FFFFFFF, 0, kShadowStagingDepth));
}

void test_staging_limit_is_a_near_bound_not_a_far_one() {
  CHECK(!stages_shadow(-1, kShadowStagingDepth, kShadowStagingDepth));
  CHECK(!stages_shadow(-1, kShadowStagingDepth + 1, kShadowStagingDepth));
  // The regression: a negated limit would reject every visible Moby, since view depth is positive.
  CHECK(stages_shadow(-1, 1, kShadowStagingDepth));
}

// The shaded pass at 0x80022A2C emits the SAME idiom as the regular pass and differs only in the
// constant, so it stages against a near bound of 0x1100:
//
//   regular  0x8001F34C  addi $t3,$v1,-0x1200 ; 0x8001F350 bgez $t3,skip -> stage iff viewZ <
//   0x1200 shaded   0x80022C30  addi $a0,$v1,-0x1100 ; 0x80022C34 bgez $a0,skip -> stage iff viewZ
//   < 0x1100
//
// These bytes were read from a RAM dump of the loaded executable. An earlier revision of this file
// asserted the shaded pair was unsatisfiable and pinned that claim as a negative case; that was
// taken from this project's own prose instead of the image and the disassembly refutes it. The
// constant is 0x1100, positive, and the shaded pass stages shadows.
void test_shaded_pass_stages_against_its_own_nearer_limit() {
  CHECK(stages_shadow(-1, 0, kShadedShadowStagingDepth));
  CHECK(stages_shadow(-1, kShadedShadowStagingDepth - 1, kShadedShadowStagingDepth));
  CHECK(!stages_shadow(-1, kShadedShadowStagingDepth, kShadedShadowStagingDepth));
  CHECK(!stages_shadow(-1, kShadedShadowStagingDepth + 1, kShadedShadowStagingDepth));
  // The far bound still requires a negative m_ShadowDistance, as 0x80022C2C's own `bgez $a0` does.
  CHECK(!stages_shadow(0, 0, kShadedShadowStagingDepth));
  CHECK(!stages_shadow(0x7FFFFFFF, 0, kShadedShadowStagingDepth));
  // The shaded limit is NEARER than the regular one, so every depth the regular pass stages the
  // shaded pass also stages. A build that swaps the two constants still passes the cases above, so
  // the ordering is pinned explicitly.
  CHECK(kShadedShadowStagingDepth < kShadowStagingDepth);
  CHECK(stages_shadow(-1, kShadedShadowStagingDepth, kShadowStagingDepth));
  CHECK(!stages_shadow(-1, kShadedShadowStagingDepth, kShadedShadowStagingDepth));
}

// One view, two answers. With a zero model radius the horizontal plane is 4*512*|x| < 3*W*z, so at
// z=1000 the guest (W=512) admits |x| <= 749 and the 16:9 draw (W=684) admits |x| <= 1001. Both
// edges, both sides: the guest answer must never widen, and the drawn answer must cover the margin.
void test_classify_view_splits_guest_and_drawn_at_both_edges() {
  constexpr int32_t kRadius = 100000;
  for (const int32_t sign : {1, -1}) {
    const auto inside = classify_view({sign * 749, 0, 1000}, 0u, kRadius, 684);
    CHECK(inside.guest.visible && inside.drawn.visible);
    const auto margin = classify_view({sign * 750, 0, 1000}, 0u, kRadius, 684);
    CHECK(!margin.guest.horizontal && !margin.guest.visible);
    CHECK(margin.drawn.horizontal && margin.drawn.visible);
    CHECK(margin.guest.flags == 0u && margin.drawn.flags != 0u);
    CHECK(classify_view({sign * 1001, 0, 1000}, 0u, kRadius, 684).drawn.visible);
    const auto beyond = classify_view({sign * 1002, 0, 1000}, 0u, kRadius, 684);
    CHECK(!beyond.drawn.horizontal && !beyond.guest.horizontal);
    // At the native width the two answers are one answer.
    const auto native = classify_view({sign * 750, 0, 1000}, 0u, kRadius, 512);
    CHECK(!native.drawn.visible && !native.guest.visible);
  }
  // Depth still rejects both: behind the radius bound nothing is drawn at any width.
  const auto behind = classify_view({0, 0, kRadius}, 0u, kRadius, 684);
  CHECK(!behind.guest.horizontal && !behind.drawn.horizontal);
}

} // namespace

int main() {
  RUN(near_moby_with_a_shadow_stages);
  RUN(shadowless_moby_never_stages);
  RUN(staging_limit_is_a_near_bound_not_a_far_one);
  RUN(shaded_pass_stages_against_its_own_nearer_limit);
  RUN(classify_view_splits_guest_and_drawn_at_both_edges);
  return pt_summary();
}
