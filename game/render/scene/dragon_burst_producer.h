#pragma once

class Core;

namespace spyro::dragon_burst {

// Native owner of the dragon-rescue burst 0x80058864. An inactive gate is a valid empty frame.
bool submit(Core *core);

} // namespace spyro::dragon_burst
