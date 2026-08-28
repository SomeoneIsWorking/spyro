#include "fx_field_cyclorama.h"

#include "core.h"
#include "cyclorama_scene_recipe.h"
#include "spyro_game.h"

#include <lucent/log.h>

bool spyro_field_cyclorama_submit(Core *core) {
  const auto recipe = spyro::cyclorama_scene_recipe::prepare(core);
  if (recipe.status != spyro::cyclorama_scene_recipe::Status::Ready) {
    lucent::debug("fieldsky",
                  "REFUSED status={} reason={} portals={} active={} valid_empty={}",
                  spyro::cyclorama_scene_recipe::statusName(recipe.status),
                  recipe.refusal,
                  recipe.portalCount,
                  recipe.activePortals,
                  recipe.validEmptyPortals);
    return false;
  }
  if (!spyro_terrain_submit(core,
                            recipe.mainSelection,
                            spyro::cyclorama_scene_recipe::kCamera + 0x14u,
                            spyro::cyclorama_scene_recipe::kCamera)) {
    return false;
  }
  spyro::cyclorama_scene_recipe::publishSpin(core, recipe);
  lucent::debug("fieldsky",
                "PASS selection={} portals={} active={} valid_empty={} yaw=0x{:X} pitch={}",
                recipe.mainSelection,
                recipe.portalCount,
                recipe.activePortals,
                recipe.validEmptyPortals,
                recipe.nextYaw,
                recipe.nextPitch);
  return true;
}
