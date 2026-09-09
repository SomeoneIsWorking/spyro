#include "fx_moby_shadow.h"

#include "core.h"
#include "game.h"
#include "moby_shadow_recipe.h"
#include "moby_shadow_submitter.h"
#include "render_queue.h"

#include <lucent/log.h>

bool spyro_moby_shadow_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  const auto recipe = spyro::moby_shadow_recipe::derive(core);
  // Denominators on every path. A frame whose shadows were all rejected has to read differently
  // from a frame that queued none, and differently again from a refusal.
  const auto census = [&recipe]() {
    return lucent::detail::format(
        "entries={} drawn={} faces={} no_plane={} far={} backfacing={} offscreen={} bin={}",
        recipe.entries,
        recipe.drawn,
        recipe.faces.size(),
        recipe.rejects[(std::size_t)spyro::moby_shadow_recipe::Reject::NoShadowPlane],
        recipe.rejects[(std::size_t)spyro::moby_shadow_recipe::Reject::BehindCamera],
        recipe.rejects[(std::size_t)spyro::moby_shadow_recipe::Reject::Backfacing],
        recipe.rejects[(std::size_t)spyro::moby_shadow_recipe::Reject::OffScreen],
        recipe.rejects[(std::size_t)spyro::moby_shadow_recipe::Reject::NegativeBin]);
  };
  if (recipe.status == spyro::moby_shadow_recipe::Status::ValidEmpty) {
    lucent::debug("mobyshadow", "PASS empty {}", census());
    return true;
  }
  if (recipe.status != spyro::moby_shadow_recipe::Status::Ready) {
    lucent::debug("mobyshadow",
                  "REFUSED recipe status={} {}",
                  spyro::moby_shadow_recipe::statusName(recipe.status),
                  census());
    return false;
  }
  const auto plan =
      spyro::moby_shadow_submitter::prepare(core, core->game->rq, recipe.faces.size());
  if (plan.status != spyro::moby_shadow_submitter::Status::Ready) {
    lucent::debug("mobyshadow", "REFUSED submitter status={} {}", (int)plan.status, census());
    return false;
  }
  spyro::moby_shadow_submitter::submit(core, core->game->rq, recipe, plan);
  lucent::debug("mobyshadow", "PASS {}", census());
  return true;
}
