// The two `bgez` branches at 0x8001F344/0x8001F350 decide whether a Moby joins the shadow list the
// moby-shadow producer 0x80059F8C later consumes. Both branches skip, and the depth they compare is
// positive, so a mis-signed limit turns the pair into a contradiction that stages nothing. That is
// exactly what shipped: every Moby shadow in the game was silently absent while the producer
// reported a healthy empty list. These cases pin both answers.
#include "actor_scene_builder.h"
#include "testutil.h"

using spyro::actor_scene::kShadowStagingDepth;
using spyro::actor_scene::stages_shadow;

namespace {

void test_near_moby_with_a_shadow_stages() {
  CHECK(stages_shadow(-1, 0));
  CHECK(stages_shadow((int32_t)0x8FC0085C, kShadowStagingDepth - 1));
}

void test_shadowless_moby_never_stages() {
  CHECK(!stages_shadow(0, 0));
  CHECK(!stages_shadow(0x7FFFFFFF, 0));
}

void test_staging_limit_is_a_near_bound_not_a_far_one() {
  CHECK(!stages_shadow(-1, kShadowStagingDepth));
  CHECK(!stages_shadow(-1, kShadowStagingDepth + 1));
  // The regression: a negated limit would reject every visible Moby, since view depth is positive.
  CHECK(stages_shadow(-1, 1));
}

} // namespace

int main() {
  RUN(near_moby_with_a_shadow_stages);
  RUN(shadowless_moby_never_stages);
  RUN(staging_limit_is_a_near_bound_not_a_far_one);
  return pt_summary();
}
