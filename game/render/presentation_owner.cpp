#include "presentation_owner.h"

#include "core.h"
#include "spyro_context.h"

SpyroPresentationOwner &spyro_presentation_owner(Core &core) {
  return spyro_context(core).presentationOwner;
}

const SpyroPresentationOwner &spyro_presentation_owner(const Core &core) {
  return spyro_context(core).presentationOwner;
}
