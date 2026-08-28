#pragma once

class Core;

// Retail FIELD setup 0x800521C0 classifies the level moby array into three
// terminated pointer lists and updates category-visibility bytes. Its retained
// body is state-only: no child calls, GPU/OT output, display tail, or VSync.
void spyro_field_build_moby_lists(Core *core);
