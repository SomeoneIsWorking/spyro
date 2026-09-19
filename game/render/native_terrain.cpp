// native_terrain.cpp — the terrain producer's guest-facing entry points (0x8004EBA8).
//
// The producer itself is three owners: `terrain_scene` reads the corpus out of guest memory,
// `terrain_recipe` projects it, and `terrain_emit` preflights and publishes it. What is left here
// is the boundary the guest calls across — a selector and two SHORTMATRIX pointers — and the one
// log line that names which stage declined.
#include "core.h"
#include "game.h"
#include "render_queue.h"
#include "spyro_context.h"
#include "terrain_emit.h"
#include "terrain_scene.h"

#include <array>
#include <cstdint>
#include <lucent/log.h>
#include <utility>

namespace {

bool submitTerrain(Core *core,
                   int32_t selector,
                   const std::array<uint32_t, 5> &cull,
                   const std::array<uint32_t, 5> &view) {
  auto &history = spyro_context(*core).terrainTemporal;
  auto captured = spyro::terrain_scene::capture(*core, selector, cull, view);
  if (captured.status != spyro::terrain_scene::Status::Ready) {
    lucent::error(
        "terraindirect", "REFUSED capture selector={} first={}", selector, captured.refusal);
    history.refuse();
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const auto prepared = spyro::terrain_emit::prepare(*core, queue, captured.input);
  if (!spyro::actor_stage::completed(prepared.status)) {
    lucent::error("terraindirect",
                  "REFUSED stage={} objects={} candidates={} rejects={} F3={} G3={} faces={} "
                  "recipe={} submitter={} first={}",
                  spyro::actor_stage::name(prepared.status),
                  prepared.recipe.objects,
                  prepared.recipe.candidates,
                  prepared.recipe.rejects,
                  prepared.recipe.f3,
                  prepared.recipe.g3,
                  prepared.recipe.faces.size(),
                  spyro::terrain_recipe::statusName(prepared.recipe.status),
                  spyro::terrain_submitter::statusName(prepared.submitter.status),
                  prepared.recipe.refusal);
    history.refuse();
    return false;
  }
  if (prepared.status == spyro::terrain_emit::Status::ValidEmpty) {
    lucent::debug("terraindirect",
                  "owned valid-empty objects={} candidates={} rejects={}",
                  prepared.recipe.objects,
                  prepared.recipe.candidates,
                  prepared.recipe.rejects);
    // An update that drew no terrain is still an endpoint: reconstructing the interval that reaches
    // it must produce the same nothing, not the update before it.
    history.retain(std::move(captured.input));
    return true;
  }
  spyro::terrain_emit::publish(*core, queue, prepared);
  history.retain(std::move(captured.input));
  return true;
}

} // namespace

bool spyro_terrain_submit(Core *c, int32_t selector, uint32_t mat1, uint32_t mat2) {
  const auto cull = spyro::terrain_scene::matrixWords(*c, mat1);
  const auto view = spyro::terrain_scene::matrixWords(*c, mat2);
  if (!cull || !view) {
    lucent::error("terraindirect", "REFUSED matrix_bounds cull=0x{:08X} view=0x{:08X}", mat1, mat2);
    return false;
  }
  return submitTerrain(c, selector, *cull, *view);
}

bool spyro_terrain_submit_matrices(Core *c,
                                   int32_t selector,
                                   const std::array<uint32_t, 5> &cull,
                                   const std::array<uint32_t, 5> &view) {
  return submitTerrain(c, selector, cull, view);
}
