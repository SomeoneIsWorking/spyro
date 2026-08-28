#pragma once

class Core;

// Complete native owner for the sign-clear, low-bit-11 world/shaded class of
// RasterizeSpritePrimQueue 0x80022A2C. Not wired into stage 0 until the entire
// field scene has source-owned producers.
bool spyro_field_shaded_queue_submit(Core *core);
