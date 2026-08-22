#pragma once

#include "fx_paired_actor.h"

// One title-level context composed from the states owned by cohesive subsystems. Adding another
// subsystem does not turn its renderer state into the definition of the whole game context.
struct SpyroContext {
  SpyroPairedActorFrameState pairedActor{};
};
