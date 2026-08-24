#include "title_runtime_registry.h"

#include "spyro1_runtime.h"
#include "spyro2_runtime.h"
#include "spyro3_runtime.h"
#include "spyro_title_catalog.generated.h"

#include <cstdlib>

namespace spyro {

std::span<const ExecutableIdentity> executableCatalog() {
  return generated::kExecutableCatalog;
}

SpyroRuntime &runtimeFor(SpyroTitle title) {
  switch (title) {
  case SpyroTitle::Spyro1: {
    static spyro1::Spyro1Runtime runtime;
    return runtime;
  }
  case SpyroTitle::Spyro2: {
    static spyro2::Spyro2Runtime runtime;
    return runtime;
  }
  case SpyroTitle::Spyro3: {
    static spyro3::Spyro3Runtime runtime;
    return runtime;
  }
  }
  std::abort();
}

} // namespace spyro
