#pragma once

class Core;

namespace spyro::paired_actor {

struct FrameState;

} // namespace spyro::paired_actor

// One live invocation of guest renderer 0x80023AC4: decode the pose, read the guest's own primitive
// stream and material table, apply the colour-fade arm, capture the frame, and hand it to the
// shared emit. It refuses rather than drawing a partial frame — every refusal names the step.
//
// It does NOT decide whether this frame is drawn, or which captured endpoint the presentation
// shows; that is the temporal owner's decision (`paired_actor_temporal.h`). This owner only answers
// "what did the guest's renderer produce on this invocation".
namespace spyro::paired_actor_producer {

// `authoredReplay` selects the field arm's replay of the authored ordering table rather than the
// stage arm's ordinary depth order. It is the one difference between the two call sites.
bool captureAndSubmit(Core *c, spyro::paired_actor::FrameState &state, bool authoredReplay);

} // namespace spyro::paired_actor_producer
