#pragma once

#include "paired_actor.h"

class Core;
class RenderQueue;

// The 60 fps half of guest renderer 0x80023AC4: it owns the two captured endpoints, the rule for
// whether a pair of them may be rebuilt, the rebuild itself, and the per-frame lifecycle that
// decides which endpoint is which.
//
// The producer half (`paired_actor_producer`) owns one live invocation — it decodes a pose, reads
// the guest's stream and material tables, and captures a `Frame`. It does NOT decide when a frame
// is drawn: the guest draws 0x80023AC4 on one field and replays the model's poses continuously on
// the next, so which endpoint is presented is this owner's decision, not the producer's.
namespace spyro::paired_actor {

// --- the lifecycle
// ---------------------------------------------------------------------------------

// Called once per drawn frame, before the guest's renderer runs. Clears the per-frame census and
// decides which of the two endpoints this frame's invocation is allowed to join.
void frameBegin(FrameState &state, bool state2, bool reference_leg, bool fps60_active);

// The ownership gate. Returns false when the frame did not present exactly the group the caller
// armed for, which its caller turns into an abort; the reason is already logged.
bool frameFinish(const FrameState &state, bool reference_leg, bool expect_group);

// --- the rebuild
// -----------------------------------------------------------------------------------

// Re-emit one captured endpoint. `authoredReplay` travels on the frame itself, so an endpoint
// replays into the same ordering it was captured from.
RebuildResult rebuildEndpoint(Core *c, RenderQueue &target, const Frame &frame);

// Emit one captured endpoint's resolved faces. BOTH halves call it — the producer for the frame it
// just captured, the temporal paths for each endpoint — so the live draw and the replay draw go
// through one emit and cannot disagree about ordering, painter accounting or the depth tie.
RebuildResult emitCapturedEndpoint(Core *c,
                                   RenderQueue &target,
                                   const Frame &frame,
                                   const GpuSnapshot &destination);

// Rebuild the pair at `t`. The two endpoints must be compatible; `t` outside [0,1] or a
// non-finite `t` is refused rather than clamped, because both mean the caller lost its own
// endpoints.
RebuildResult rebuildSample(
    Core *core, RenderQueue &target, const Frame &previous, const Frame &current, float t);

// --- the 60 fps drivers
// ----------------------------------------------------------------------------

// The field rotation: the frame just presented becomes the previous endpoint.
void fps60Rotate(Core *c);

// The in-between: rebuild the pair at `t` into the frame's redirected sink.
void fps60WorldPass(Core *c, float t);

// Whether the current endpoint pair can be rebuilt at all. Runs the whole rebuild at the midpoint
// and refuses on any step, so a pair is admitted only if its midpoint is drawable.
bool fps60Eligible(FrameState &state);

// --- the owner's own rules, exposed for its selftest
// ------------------------------------------------ These are not a second API: they are the
// decisions this owner makes, named so the hermetic selftest exercises the shipping rules rather
// than a restatement of them.

[[nodiscard]] bool framesCompatible(const Frame &a, const Frame &b);
[[nodiscard]] bool rebuildRecipeEligible(const Frame &frame, bool duplicate);
[[nodiscard]] bool projectCaptured(const Frame &frame, std::vector<ProjectedVertex> &out);
[[nodiscard]] bool interpolateProjected(std::span<const ProjectedVertex> a,
                                        std::span<const ProjectedVertex> b,
                                        const Transform &transform,
                                        float t,
                                        std::vector<ProjectedVertex> &out);

// The GPU snapshot a rebuild writes INTO. Always the CURRENT frame's, even for a forced `t == 0`,
// because the frame the guest is presenting is the current one whatever content it draws.
[[nodiscard]] const GpuSnapshot &temporalDestination(const Frame &previous, const Frame &current);

} // namespace spyro::paired_actor
