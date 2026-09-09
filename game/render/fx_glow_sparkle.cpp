#include "fx_glow_sparkle.h"

#include "core.h"
#include "game.h"
#include "glow_recipe.h"
#include "glow_submitter.h"
#include "render_queue.h"
#include "sparkle_recipe.h"
#include "sparkle_submitter.h"

#include <lucent/log.h>

namespace {

constexpr unsigned kDeltaTime = 0x800756CCu;

bool submitGlows(Core *core) {
  const auto recipe = spyro::glow_recipe::derive(core);
  const auto census = [&recipe]() {
    lucent::Line line;
    line.add("records={} drawn={} faces={}", recipe.records, recipe.drawn, recipe.faces.size());
    line.add(" empty={} no_depth={} negative_bin={} offscreen={}",
             recipe.rejects[(std::size_t)spyro::glow_recipe::Reject::EmptyRecord],
             recipe.rejects[(std::size_t)spyro::glow_recipe::Reject::NoDepth],
             recipe.rejects[(std::size_t)spyro::glow_recipe::Reject::NegativeBin],
             recipe.rejects[(std::size_t)spyro::glow_recipe::Reject::Offscreen]);
    return line;
  };
  if (recipe.status == spyro::glow_recipe::Status::ValidEmpty) {
    census().flush_debug("glow");
    return true;
  }
  if (recipe.status != spyro::glow_recipe::Status::Ready) {
    lucent::Line line;
    line.add("REFUSED recipe={}", spyro::glow_recipe::statusName(recipe.status));
    census().flush_debug("glow");
    line.flush_debug("glow");
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const auto plan = spyro::glow_submitter::prepare(queue, recipe);
  if (plan.status != spyro::glow_submitter::Status::Ready) {
    lucent::Line line;
    line.add("REFUSED submitter status={}", (int)plan.status);
    line.flush_debug("glow");
    return false;
  }
  spyro::glow_submitter::submit(core, queue, recipe, plan);
  census().flush_debug("glow");
  return true;
}

bool submitSparkles(Core *core) {
  const int32_t deltaTime = (int32_t)core->mem_r32(kDeltaTime);
  const auto recipe = spyro::sparkle_recipe::derive(core, deltaTime);
  const auto census = [&recipe, deltaTime]() {
    lucent::Line line;
    line.add("dt={} alive={} drawn={} lines={}",
             deltaTime,
             recipe.alive,
             recipe.drawn,
             recipe.lines.size());
    line.add(" expired={} no_lifetime={} too_far={} too_near={} offscreen={}",
             recipe.rejects[(std::size_t)spyro::sparkle_recipe::Reject::Expired],
             recipe.rejects[(std::size_t)spyro::sparkle_recipe::Reject::NoLifetime],
             recipe.rejects[(std::size_t)spyro::sparkle_recipe::Reject::TooFar],
             recipe.rejects[(std::size_t)spyro::sparkle_recipe::Reject::TooNear],
             recipe.rejects[(std::size_t)spyro::sparkle_recipe::Reject::Offscreen]);
    return line;
  };
  if (recipe.status != spyro::sparkle_recipe::Status::Ready &&
      recipe.status != spyro::sparkle_recipe::Status::ValidEmpty) {
    lucent::Line line;
    line.add("REFUSED recipe={}", spyro::sparkle_recipe::statusName(recipe.status));
    census().flush_debug("sparkle");
    line.flush_debug("sparkle");
    return false;
  }
  RenderQueue &queue = core->game->rq;
  spyro::sparkle_submitter::Plan plan{};
  if (recipe.status == spyro::sparkle_recipe::Status::Ready) {
    plan = spyro::sparkle_submitter::prepare(queue, recipe);
    if (plan.status != spyro::sparkle_submitter::Status::Ready) {
      lucent::Line line;
      line.add("REFUSED submitter status={}", (int)plan.status);
      line.flush_debug("sparkle");
      return false;
    }
  }
  // The lifetime burn belongs to the frame whether or not the sparkle was drawn, and it is only
  // committed once the frame is certain to be accepted: a refused submission that had already aged
  // the records would drop a tick of every sparkle's life.
  spyro::sparkle_recipe::commit(core, recipe);
  if (recipe.status == spyro::sparkle_recipe::Status::Ready) {
    spyro::sparkle_submitter::submit(core, queue, recipe, plan);
  }
  census().flush_debug("sparkle");
  return true;
}

} // namespace

bool glow_sparkle_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  // 0x80058BA8 calls the glows first and the sparkles second, and the sparkle half must still run
  // its state advance when the glow half produced nothing.
  const bool glows = submitGlows(core);
  const bool sparkles = submitSparkles(core);
  return glows && sparkles;
}
