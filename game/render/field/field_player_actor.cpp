#include "field_player_actor.h"

#include "core.h"
#include "paired_actor.h"
#include "producer_refusal.h"

namespace {

constexpr uint32_t kIsSpyroHidden = 0x80075814u;
constexpr uint32_t kProducer = 0x80023AC4u;

} // namespace

bool spyro::field_player_actor::visible(Core *core) {
  return core != nullptr && spyro::field_player_actor::visible(core->mem_r32(kIsSpyroHidden));
}

spyro::ProducerRefusal spyro::field_player_actor::submit(Core *core,
                                                         spyro::paired_actor::FrameState &state) {
  if (core == nullptr) {
    return spyro::refuse("pairedactor", kProducer, "no core");
  }
  if (!spyro::field_player_actor::visible(core)) {
    return {};
  }
  if (spyro::paired_actor::submitField(core, state)) {
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
