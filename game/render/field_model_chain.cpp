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

namespace {

// A layer that only answers yes or no still names itself; the composed pass answers with what
// it saw, and that detail travels unchanged to the fatal boundary.
spyro::ProducerRefusal layer(bool composed, uint32_t producer) {
  return composed ? spyro::ProducerRefusal{} : spyro::ProducerRefusal{producer, {}};
}

} // namespace

spyro::ProducerRefusal spyro_field_model_chain_submit(Core *core) {
  // This one names its own reason, so it is returned intact rather than flattened through `layer`.
  if (const auto refusal = spyro_actor_submit(core)) {
    return refusal;
  }
  if (const auto refusal = spyro_field_actor_composition_submit(core)) {
    return refusal;
  }
  // 0x80019698 draws the moby shadows between the shaded pass and Spyro's own model, so this layer
  // belongs here rather than beside the Spyro shadow it superficially resembles.
  if (const auto refusal = layer(spyro_moby_shadow_submit(core), 0x80059F8Cu)) {
    return refusal;
  }
  // Another that names its own reason, so it too is returned intact rather than flattened.
  if (const auto refusal = spyro_field_player_submit(core, spyro_paired_actor_state(core))) {
    return refusal;
  }
  if (const auto refusal = layer(spyro_field_shadow_submit(core), 0x80059A48u)) {
    return refusal;
  }
  // The flame is called last of the model layers, only while it is active, and after Spyro's own
  // producer has published the orientation it reads.
  if (const auto refusal = layer(spyro_flame_submit(core), 0x80058D64u)) {
    return refusal;
  }
  // The last call: glow halos then sparkles. The sparkle half also ages and kills its own records,
  // so this must run every field, not only when something is visible.
  if (const auto refusal = layer(glow_sparkle_submit(core), 0x80058BA8u)) {
    return refusal;
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
  return {};
}
