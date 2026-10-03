#include "presentation_owner.h"

#include "core.h"
#include "spyro_context.h"

spyro::PresentationOwner &spyro::presentationOwner(Core &core) {
  return spyro::context(core).presentationOwner;
}

const spyro::PresentationOwner &spyro::presentationOwner(const Core &core) {
  return spyro::context(core).presentationOwner;
}
