#include "menu_world_pass.h"

#include "core.h"
#include "cutscene_scene_recipe.h"
#include "field_cyclorama.h"
#include "field_environment.h"
#include "field_moby_lists.h"
#include "field_model_chain.h"
#include "field_particles.h"

#include <lucent/log.h>

namespace spyro::menu_world {
namespace {

// 0x80019698's lifecycle clear, at the guest's own position immediately before the moby build: it
// terminates the previous screen's shaded queue. The FIELD arm reproduces the same write at
// 0x800720F4, and omitting it leaves the field HUD capacity consumed by a stale screen.
constexpr std::uint32_t kShadedMobyList = 0x800720F4u;

} // namespace

Refusal submit(Core *core) {
  const auto background = spyro::cutscene_scene_recipe::read(core);
  spyro::cutscene_scene_recipe::prepareFrame(core, background);
  core->mem_w32(kShadedMobyList, 0u);
  spyro::field_moby_lists::build(core);
  if (const auto refusal = spyro::field_model_chain::submit(core)) {
    lucent::debug("render", "REFUSED 0x80019698: {}", refusal.detail);
    return Refusal::ActorChain;
  }
  if (const auto refusal = spyro::field_particles::submit(core)) {
    lucent::debug("render", "REFUSED 0x800573C8: {}", refusal.detail);
    return Refusal::Particles;
  }
  if (!spyro::field_cyclorama::submit(core)) {
    return Refusal::Cyclorama;
  }
  if (!spyro::field_environment::submit(core)) {
    return Refusal::Environment;
  }
  return Refusal::None;
}

} // namespace spyro::menu_world
