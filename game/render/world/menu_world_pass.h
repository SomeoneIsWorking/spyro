// menu_world_pass.h — the world a menu draws behind itself.
//
// The pause / inventory handler 0x8001A40C and the fairy handler 0x8001D718 both run the FIELD
// arm's world calls and none of its 2D layers: 0x800521C0 (moby list build), 0x80019698 (actor
// pass), 0x800573C8 (particles), 0x80050BD0 (cyclorama) and 0x8002B9CC (environment). They call the
// same producers over the same state, so this is the one place the sequence is written down.
#pragma once

class Core;

namespace spyro::menu_world {

// Which call refused, so the caller reports a name rather than a bare address.
enum class Refusal { None, ActorChain, Particles, Cyclorama, Environment };

// Frame preparation (the cyclorama background) and the five world producers, in the field arm's
// order. g_SonyImage's shaded list is terminated at the guest's own position before the moby build
// so a previous screen's HUD mobys cannot leak into this one.
Refusal submit(Core *core);

} // namespace spyro::menu_world
