#include "fx_field_player_actor.h"

#include "core.h"
#include "fx_paired_actor.h"
#include "producer_refusal.h"

namespace {

constexpr uint32_t kIsSpyroHidden = 0x80075814u;
constexpr uint32_t kProducer = 0x80023AC4u;

} // namespace

bool spyro_field_player_visible(Core *core) {
  return core != nullptr && spyro_field_player_visible(core->mem_r32(kIsSpyroHidden));
}

spyro::ProducerRefusal spyro_field_player_submit(Core *core, SpyroPairedActorFrameState &state) {
  if (core == nullptr) {
    return spyro::refuse("pairedactor", kProducer, "no core");
  }
  if (!spyro_field_player_visible(core)) {
    return {};
  }
  if (spyro_paired_actor_submit_field(core, state)) {
    return {};
  }
  return spyro::refuse("pairedactor",
                       kProducer,
                       "{} (invocations={} groups={} candidates={} faces={})",
                       state.refusal == nullptr ? "no reason recorded" : state.refusal,
                       state.invocations,
                       state.groups,
                       state.candidates,
                       state.faces);
}
