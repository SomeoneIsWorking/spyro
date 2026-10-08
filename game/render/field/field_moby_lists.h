#pragma once

class Core;

namespace spyro::field_moby_lists {

// Retail FIELD setup 0x800521C0 classifies the level moby array into three
// terminated pointer lists and updates category-visibility bytes. Its retained
// body is state-only: no child calls, GPU/OT output, display tail, or VSync.
void build(Core *core);

} // namespace spyro::field_moby_lists
