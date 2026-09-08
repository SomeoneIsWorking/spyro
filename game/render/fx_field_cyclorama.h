#pragma once

class Core;

// Native FIELD cyclorama wrapper 0x80050BD0 for frames with no visible portal
// aperture. Production-compiled but not wired until the complete stage-0 scene
// is owned.
bool spyro_field_cyclorama_submit(Core *core);
