#pragma once

class Core;

namespace spyro::field_cyclorama {

// Native FIELD cyclorama wrapper 0x80050BD0 for frames with no visible portal
// aperture. Production-compiled but not wired until the complete stage-0 scene
// is owned.
bool submit(Core *core);

} // namespace spyro::field_cyclorama
