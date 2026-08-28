#pragma once

class Core;

namespace spyro1 {

// Skip the level-transition tally/HUD on a fresh Start edge. Loading and the guest transition
// state remain intact; this is deliberately not a teleport or a CD-load bypass.
bool skipLevelTransition(Core &core, bool startEdge);

} // namespace spyro1
