#include "painter_submission_preflight.h"

#include "painter_object_layer.h"
#include "render_queue.h"

namespace spyro::painter_submission {

Plan preflight(const RenderQueue &queue, uint32_t object, size_t newFaces, uint32_t replayDomain) {
  Plan plan{};
  const auto admission = queue.preflightPainterObject(object, newFaces, replayDomain);
  plan.ready = admission.accepted();
  plan.queued = (int)admission.queued_items;
  plan.existingObjects = admission.existing_objects;
  plan.existingFaces = admission.existing_faces;
  return plan;
}

} // namespace spyro::painter_submission
