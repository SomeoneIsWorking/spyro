#pragma once

struct Core;

// Direct native owner of 0x80058BA8, the last call of 0x80019698. It is two producers in one:
// 0x800580F4 draws sixteen glow halos as untextured additive fans, then 0x800584C4 draws eight
// sparkles as crossed GP0 lines. The second half also ADVANCES the sparkles — it burns each
// lifetime by g_DeltaTime, spins the angle, and kills a sparkle it declines to draw — so this is
// the one render producer in the port that writes guest state, and skipping it would leave
// sparkles alive forever. Returns false only when either half cannot represent the live state.
bool glow_sparkle_submit(Core *core);
