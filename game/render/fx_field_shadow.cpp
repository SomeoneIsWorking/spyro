#include "fx_field_shadow.h"

#include "core.h"
#include "field_shadow_recipe.h"
#include "field_shadow_submitter.h"
#include "game.h"
#include "render_queue.h"

#include <cstdint>
#include <lucent/log.h>

bool spyro_field_shadow_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  const auto recipe = spyro::field_shadow_recipe::derive(core);
  if (recipe.status == spyro::field_shadow_recipe::Status::ValidEmpty) {
    return true;
  }
  if (recipe.status != spyro::field_shadow_recipe::Status::Ready) {
    lucent::debug("fieldshadow",
                  "REFUSED recipe status={} faces={}",
                  spyro::field_shadow_recipe::statusName(recipe.status),
                  recipe.faceCount);
    return false;
  }
  const auto plan = spyro::field_shadow_submitter::prepare(core->game->rq, recipe.faceCount);
  if (plan.status != spyro::field_shadow_submitter::Status::Ready) {
    lucent::debug("fieldshadow", "REFUSED queue faces={}", recipe.faceCount);
    return false;
  }
  spyro::field_shadow_submitter::submit(core, core->game->rq, recipe, plan);
  lucent::debug("fieldshadow", "PASS faces={}", recipe.faceCount);
  return true;
}
