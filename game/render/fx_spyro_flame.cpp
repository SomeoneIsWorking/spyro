#include "fx_spyro_flame.h"

#include "core.h"
#include "game.h"
#include "render_queue.h"
#include "spyro_flame_recipe.h"
#include "spyro_flame_submitter.h"

#include <lucent/log.h>

namespace {

constexpr unsigned kFlameActive = 0x80078760u; // g_SpyroFlame + 0x98

} // namespace

bool spyro_flame_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  if (core->mem_r8(kFlameActive) == 0u) {
    return true;
  }
  const auto recipe = spyro::flame_recipe::derive(core);
  const auto census = [&recipe]() {
    lucent::Line line;
    line.add("parts={} tips={} ribbons={} faces={}",
             recipe.parts,
             recipe.tips,
             recipe.ribbons,
             recipe.faces.size());
    line.add(" empty_part={} past_limit={} tip_behind={} tip_backfacing={} ribbon_bin={}",
             recipe.rejects[(std::size_t)spyro::flame_recipe::Reject::EmptyPart],
             recipe.rejects[(std::size_t)spyro::flame_recipe::Reject::TipCursorPastLimit],
             recipe.rejects[(std::size_t)spyro::flame_recipe::Reject::TipBehindCamera],
             recipe.rejects[(std::size_t)spyro::flame_recipe::Reject::TipBackfacing],
             recipe.rejects[(std::size_t)spyro::flame_recipe::Reject::RibbonNegativeBin]);
    return line;
  };
  if (recipe.status == spyro::flame_recipe::Status::ValidEmpty) {
    census().flush_debug("spyroflame");
    return true;
  }
  if (recipe.status != spyro::flame_recipe::Status::Ready) {
    lucent::Line line;
    line.add("REFUSED recipe={} ", spyro::flame_recipe::statusName(recipe.status));
    census().flush_debug("spyroflame");
    line.flush_debug("spyroflame");
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const auto plan = spyro::flame_submitter::prepare(queue, recipe);
  if (plan.status != spyro::flame_submitter::Status::Ready) {
    lucent::Line line;
    line.add("REFUSED submitter status={}", (int)plan.status);
    line.flush_debug("spyroflame");
    return false;
  }
  spyro::flame_submitter::submit(core, queue, recipe, plan);
  census().flush_debug("spyroflame");
  return true;
}
