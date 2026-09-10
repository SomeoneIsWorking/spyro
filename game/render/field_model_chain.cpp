#include "field_model_chain.h"

#include "actor_scene_oracle.h"
#include "fx_actor_draw.h"
#include "fx_field_actor_composition.h"
#include "fx_field_player_actor.h"
#include "fx_field_shadow.h"
#include "fx_glow_sparkle.h"
#include "fx_moby_shadow.h"
#include "fx_paired_actor.h"
#include "fx_spyro_flame.h"

#include <array>
#include <cstdint>

unsigned spyro_field_model_chain_submit(Core *core) {
  if (!spyro_actor_submit(core)) {
    return 0x8001F798u;
  }
  if (!spyro_field_actor_composition_submit(core)) {
    return 0x80020F34u;
  }
  // 0x80019698 draws the moby shadows between the shaded pass and Spyro's own model, so this layer
  // belongs here rather than beside the Spyro shadow it superficially resembles.
  if (!spyro_moby_shadow_submit(core)) {
    return 0x80059F8Cu;
  }
  if (!spyro_field_player_submit(core, spyro_paired_actor_state(core))) {
    return 0x80023AC4u;
  }
  if (!spyro_field_shadow_submit(core)) {
    return 0x80059A48u;
  }
  // The flame is called last of the model layers, only while it is active, and after Spyro's own
  // producer has published the orientation it reads.
  if (!spyro_flame_submit(core)) {
    return 0x80058D64u;
  }
  // The last call: glow halos then sparkles. The sparkle half also ages and kills its own records,
  // so this must run every field, not only when something is visible.
  if (!glow_sparkle_submit(core)) {
    return 0x80058BA8u;
  }
  // Diagnostic only and a no-op unless PSXPORT_ACTOR_SCENE_ORACLE=1. It runs retail's moby-chain
  // walker over the state the native producers have just read, so it must sit after every producer
  // that walker covers, and must never be armed on a shipping frame. Retail draws the player and
  // its shadow as ordinary mobys, so those two producers are part of the comparison even though the
  // port owns them separately. The printed painter histogram is what makes the split readable.
  static constexpr std::array<uint32_t, 9> kActorPainters = {0x8001F798u,
                                                             0x80020F34u,
                                                             0x80022A2Cu,
                                                             0x80023AC4u,
                                                             0x80059A48u,
                                                             0x80059F8Cu,
                                                             0x80058D64u,
                                                             0x800580F4u,
                                                             0x800584C4u};
  spyro::actor_scene_oracle::compare(core, 0x80019698u, kActorPainters, "actor-scene-oracle");
  return 0u;
}
