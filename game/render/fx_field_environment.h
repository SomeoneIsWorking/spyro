#pragma once

class Core;

namespace spyro::field_environment {

// Complete native FIELD environment wrapper 0x8002B9CC. Production-compiled
// but not wired until the complete stage-0 scene is atomically owned.
bool submit(Core *core);

} // namespace spyro::field_environment
