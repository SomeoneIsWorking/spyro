#include "spyro_runtime.h"

namespace spyro {

SpyroRuntime::SpyroRuntime(const GuestProgramImage &programImage, SpyroTitle title)
    : programImage_(programImage), title_(title) {}

const GuestProgramImage *SpyroRuntime::guestProgramImage() const {
  return &programImage_;
}

SpyroTitle SpyroRuntime::title() const {
  return title_;
}

} // namespace spyro
