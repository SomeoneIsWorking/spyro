// guest_projection_owner.h — the per-Core seam a title's own projection owner publishes into.
//
// WHY IT EXISTS. A native override is a plain `void (*)(Core *)`: there is nowhere to hang a
// user-data pointer. An override that has to consult a title-owned object — Spyro 2's widescreen
// owner is the only one today — therefore needs a way back to that object from the Core alone, and
// `spyro_context(core).projectionHook` is it.
//
// It is deliberately NOT a general "call back into the title" door. It is ONE narrow event: a
// guest projection was just restated. A title with no projection of its own leaves the pointer null
// and nothing reads it. The per-field boundary is `spyro::FieldObserver`.
#pragma once

class Core;

class GuestProjectionOwner {
public:
  virtual ~GuestProjectionOwner() = default;

  // The guest has just written CR24/CR25/CR26 through one of the sites the owner registered. The
  // retail write has already happened; the owner may re-assert whatever its plan requires.
  virtual void onProjectionPublished(Core &core) = 0;
};
