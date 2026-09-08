#pragma once

#include "archive_transfer.h"
#include "fx_paired_actor.h"
#include "presentation_owner.h"
#include "runtime_run.h"

class Core;

// One title-level context composed from the states owned by cohesive subsystems. Adding another
// subsystem does not turn its renderer state into the definition of the whole game context.
struct SpyroContext {
  spyro::ArchiveTransfer archiveTransfer{};
  spyro::RuntimeRun run{};
  SpyroPairedActorFrameState pairedActor{};
  SpyroPresentationOwner presentationOwner{};
};

SpyroContext &spyro_context(Core &core);
const SpyroContext &spyro_context(const Core &core);
