#pragma once

struct Core;

// 0x80059F8C — the shadow fan under every Moby that queued one this frame. Distinct from
// spyro_field_shadow_submit, which owns Spyro's own sixteen-point shadow 0x80059A48.
bool spyro_moby_shadow_submit(Core *core);
