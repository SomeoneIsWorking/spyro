#pragma once

#include "spyro_runtime.h"

#include <span>

namespace spyro {

std::span<const ExecutableIdentity> executableCatalog();
SpyroRuntime &runtimeFor(SpyroTitle title);

} // namespace spyro
