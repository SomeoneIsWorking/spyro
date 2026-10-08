#include "field_model_chain.h"

#include "actor_producer.h"
#include "field_actor_composition.h"
#include "field_player_actor.h"
#include "field_shadow.h"
#include "glow_sparkle.h"
#include "moby_shadow.h"
#include "paired_actor.h"
#include "spyro_flame.h"

#include <array>
#include <cstdint>

namespace {

// A layer that only answers yes or no still names itself; the composed pass answers with what
// it saw, and that detail travels unchanged to the fatal boundary.
spyro::ProducerRefusal layer(bool composed, uint32_t producer) {
  return composed ? spyro::ProducerRefusal{} : spyro::ProducerRefusal{producer, {}};
}

} // namespace

spyro::ProducerRefusal spyro::field_model_chain::submit(Core *core) {
  // This one names its own reason, so it is returned intact rather than flattened through `layer`.
  if (const auto refusal = spyro::actor_draw::submit(core)) {
    return refusal;
  }
  if (const auto refusal = spyro_field_actor_composition_submit(core)) {
    return refusal;
  }
  // 0x80019698 draws the moby shadows between the shaded pass and Spyro's own model, so this layer
  // belongs here rather than beside the Spyro shadow it superficially resembles.
  if (const auto refusal = layer(spyro::moby_shadow::submit(core), 0x80059F8Cu)) {
    return refusal;
  }
  // Another that names its own reason, so it too is returned intact rather than flattened.
  if (const auto refusal =
          spyro::field_player_actor::submit(core, spyro::paired_actor::state(core))) {
    return refusal;
  }
  if (const auto refusal = layer(spyro::field_shadow::submit(core), 0x80059A48u)) {
    return refusal;
  }
  // The flame is called last of the model layers, only while it is active, and after Spyro's own
  // producer has published the orientation it reads.
  if (const auto refusal = layer(spyro::flame::submit(core), 0x80058D64u)) {
    return refusal;
  }
  // The last call: glow halos then sparkles. The sparkle half also ages and kills its own records,
  // so this must run every field, not only when something is visible.
  if (const auto refusal = layer(spyro::glow_sparkle::submit(core), 0x80058BA8u)) {
    return refusal;
  }
  return {};
}
