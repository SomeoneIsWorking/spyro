#include "fx_field_cyclorama.h"

#include "core.h"
#include "cyclorama_mask_recipe.h"
#include "cyclorama_mask_submitter.h"
#include "cyclorama_portal_submitter.h"
#include "cyclorama_scene_recipe.h"
#include "game.h"
#include "spyro_game.h"

#include <lucent/log.h>
#include <vector>

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
  std::vector<spyro::cyclorama_mask_recipe::Recipe> maskRecipes;
  std::vector<spyro::cyclorama_mask_submitter::Draw> maskDraws;
  std::vector<spyro::cyclorama_portal_mesh::Recipe> portalRecipes;
  std::vector<spyro::cyclorama_portal_submitter::Draw> farDraws;
  std::vector<spyro::cyclorama_portal_submitter::Draw> nearDraws;
  maskRecipes.reserve(recipe.portalFrames.size());
  maskDraws.reserve(recipe.portalFrames.size());
  portalRecipes.reserve(recipe.portalFrames.size());
  farDraws.reserve(recipe.portalFrames.size());
  nearDraws.reserve(recipe.portalFrames.size());
  for (const auto &frame : recipe.portalFrames) {
    maskRecipes.push_back(spyro::cyclorama_mask_recipe::build(core, frame));
    maskDraws.push_back({&maskRecipes.back()});
    if (frame.status == spyro::cyclorama_portal_mesh::Status::Ready ||
        frame.status == spyro::cyclorama_portal_mesh::Status::NearFamilyUnsupported) {
      portalRecipes.push_back(spyro::cyclorama_portal_mesh::build(core, frame));
      auto *draw = &portalRecipes.back();
      if (frame.status == spyro::cyclorama_portal_mesh::Status::NearFamilyUnsupported) {
        nearDraws.push_back({&frame, draw});
      } else {
        farDraws.push_back({&frame, draw});
      }
    }
  }
  const auto maskPlan = spyro::cyclorama_mask_submitter::prepare(core, core->game->rq, maskDraws);
  if (maskPlan.status != spyro::cyclorama_mask_submitter::Status::Ready &&
      maskPlan.status != spyro::cyclorama_mask_submitter::Status::ValidEmpty) {
    lucent::debug("fieldsky",
                  "REFUSED mask status={} reason={}",
                  spyro::cyclorama_mask_submitter::statusName(maskPlan.status),
                  maskRecipes.empty() ? "none" : maskRecipes.front().refusal);
    return false;
  }
  const auto farPlan = spyro::cyclorama_portal_submitter::prepare(
      core, core->game->rq, spyro::cyclorama_portal_mesh::kProducerKey, farDraws);
  if (farPlan.status != spyro::cyclorama_portal_submitter::Status::Ready &&
      farPlan.status != spyro::cyclorama_portal_submitter::Status::ValidEmpty) {
    return false;
  }
  const auto nearPlan = spyro::cyclorama_portal_submitter::prepare(
      core, core->game->rq, spyro::cyclorama_portal_mesh::kNearProducerKey, nearDraws);
  if (nearPlan.status != spyro::cyclorama_portal_submitter::Status::Ready &&
      nearPlan.status != spyro::cyclorama_portal_submitter::Status::ValidEmpty) {
    return false;
  }
  spyro::cyclorama_mask_submitter::submit(core, core->game->rq, maskDraws, maskPlan);
  spyro::cyclorama_portal_submitter::submit(core, core->game->rq, farDraws, farPlan);
  spyro::cyclorama_portal_submitter::submit(core, core->game->rq, nearDraws, nearPlan);
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
