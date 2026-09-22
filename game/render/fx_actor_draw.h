#pragma once

#include "actor_scene_builder.h"
#include "producer_refusal.h"

#include <cstdint>

class Core;

namespace spyro::actor_draw {

// The guest renderer this owner replaces. Its one home: the temporal reconstruction of this
// producer's items has to name the same key the live submission publishes under, and two spellings
// of one address is how a reconstruction silently stops matching the frame it replaces.
inline constexpr uint32_t kProducerKey = 0x8001F798u;
inline constexpr const char *kProducerName = "actor:opaque";

} // namespace spyro::actor_draw

// Direct native owner of regular actor renderer 0x8001F798. The source names where 0x8001F158 took
// its Moby pointers from: FIELD's own level-array classification by default, or the explicit list a
// cutscene wrote into g_SonyImage.u.m_Draw.m_Moby. Refuses before queue mutation when the
// current record corpus contains an unsupported arm or material, and the returned refusal carries
// the stage, the recipe reason and the census that decided it — the abort a user sees is the whole
// diagnosis, not an address whose explanation lives on a debug channel nobody enabled.
spyro::ProducerRefusal spyro_actor_submit(Core *c, spyro::actor_scene::Source source = {});
