#pragma once

struct GameConfig;
struct GameHooks;

namespace spyro::legacy {

// Bounded compatibility views for framework facts/callbacks that have not yet received typed seams.
// Runtime behavior is owned by Spyro1Runtime, not by these bags.
const GameConfig &measuredConfig();
const GameHooks &compatibilityHooks();

} // namespace spyro::legacy
