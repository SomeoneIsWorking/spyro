#pragma once

#include "actor_scene_builder.h"

class Core;

// Direct native owner of regular actor renderer 0x8001F798. The source names where 0x8001F158 took
// its Moby pointers from: FIELD's own level-array classification by default, or the explicit list a
// cutscene wrote into g_SonyImage.u.m_Draw.m_Moby. Returns false before queue mutation when the
// current record corpus contains an unsupported arm or material.
bool spyro_actor_submit(Core *c, spyro::actor_scene::Source source = {});
