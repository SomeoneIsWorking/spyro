#include "title_runtime_registry.h"

#include "spyro1_runtime.h"
#include "spyro2_runtime.h"
#include "spyro3_runtime.h"
#include "spyro_title_catalog.generated.h"

#include <cstdlib>

namespace spyro {

std::string_view SpyroCatalog::productName() const {
  return "Spyro";
}

std::span<const psx::host::TitleIdentity> SpyroCatalog::titles() const {
  return generated::kExecutableCatalog;
}

SpyroTitle SpyroCatalog::title(std::size_t index) const {
  return generated::kCatalogTitles.at(index);
}

SpyroRuntime &SpyroCatalog::runtime(std::size_t index) const {
  return runtimeFor(title(index));
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
