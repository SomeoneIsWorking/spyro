// The per-Core seam a title's own projection owner publishes into. An override is a plain
// `void (*)(Core *)` with nowhere to hang user data, so an override that consults a title-owned
// object needs this way back from the Core alone. It is one narrow event, a guest projection just
// restated; the per-field boundary is `FieldObserver`.
#pragma once

class Core;

namespace spyro {

class GuestProjectionOwner {
public:
  virtual ~GuestProjectionOwner() = default;

  // The guest has just written CR24/CR25/CR26 through a registered site; the retail write has
  // already happened and the owner may re-assert whatever its plan requires.
  virtual void onProjectionPublished(Core &core) = 0;
};

} // namespace spyro
