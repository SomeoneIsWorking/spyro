#include "fx_field_player_actor.h"

#include "core.h"
#include "fx_paired_actor.h"

namespace {

constexpr uint32_t kIsSpyroHidden = 0x80075814u;

} // namespace

bool spyro_field_player_visible(Core *core) {
  return core != nullptr && spyro_field_player_visible(core->mem_r32(kIsSpyroHidden));
}

bool spyro_field_player_submit(Core *core, SpyroPairedActorFrameState &state) {
  if (core == nullptr) {
    return false;
  }
  if (!spyro_field_player_visible(core)) {
    return true;
  }
  return spyro_paired_actor_submit_field(core, state);
}
