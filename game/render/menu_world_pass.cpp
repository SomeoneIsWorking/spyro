#include "menu_world_pass.h"

#include "core.h"
#include "cutscene_scene_recipe.h"
#include "field_moby_lists.h"
#include "field_model_chain.h"
#include "fx_field_cyclorama.h"
#include "fx_field_environment.h"
#include "fx_field_particles.h"

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
  spyro_field_build_moby_lists(core);
  if (const auto refusal = spyro_field_model_chain_submit(core)) {
    lucent::debug("render", "REFUSED 0x80019698: {}", refusal.detail);
    return Refusal::ActorChain;
  }
  if (const auto refusal = spyro_field_particles_submit(core)) {
    lucent::debug("render", "REFUSED 0x800573C8: {}", refusal.detail);
    return Refusal::Particles;
  }
  if (!spyro_field_cyclorama_submit(core)) {
    return Refusal::Cyclorama;
  }
  if (!spyro_field_environment_submit(core)) {
    return Refusal::Environment;
  }
  return Refusal::None;
}

} // namespace spyro::menu_world
