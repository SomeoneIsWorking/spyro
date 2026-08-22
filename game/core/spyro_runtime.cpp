#include "spyro_runtime.h"

namespace spyro {

SpyroRuntime::SpyroRuntime(const GuestProgramImage &programImage) : programImage_(programImage) {}

const GuestProgramImage *SpyroRuntime::guestProgramImage() const {
  return &programImage_;
}

} // namespace spyro
