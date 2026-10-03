#pragma once

struct Core;

namespace spyro::moby_shadow {

// 0x80059F8C — the shadow fan under every Moby that queued one this frame. Distinct from
// spyro_field_shadow_submit, which owns Spyro's own sixteen-point shadow 0x80059A48.
bool submit(Core *core);

} // namespace spyro::moby_shadow
