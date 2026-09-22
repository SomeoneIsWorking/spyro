#include "wide_screen_space.h"

#include "core.h"
#include "gpu_vk.h"

namespace spyro::wide_screen_space {
namespace {

// The horizontal offset the port projected with, minus the one the guest's own projection uses.
// Zero unless the wide engine widened the window.
int32_t horizontalOffsetDelta(Core *core) {
  if (!gpu_vk_wide_engine(core)) {
    return 0;
  }
  return horizontalCenter(core) - (int32_t)core->rsub.projParams.geomOfx();
}

} // namespace

int32_t horizontalCenter(Core *core) {
  // The framework already answers this, for both aspects: gpu_vk_wide_engine_ofx is the render
  // width for the selected aspect divided by two, and that width collapses to the guest's own
  // GP1 horizontal resolution when the wide engine is off. This used to spell the wide half-width
  // here and fall back to projParams.geomOfx(), which is a second implementation of the same
  // number -- MEASURED equal at both aspects on the class-83 route before the delegation landed.
  return (int32_t)gpu_vk_wide_engine_ofx(core);
}

int drawClipRight(Core *core) {
  if (gpu_vk_wide_engine(core)) {
    return gpu_vk_wide_engine_w(core);
  }
  return kGuestClipRight;
}

psxport::native_projection::ProjectionParams projection(Core *core) {
  psxport::native_projection::ProjectionParams out{};
  out.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  out.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  out.h = (uint16_t)core->rsub.projParams.geomH();
  if (gpu_vk_wide_engine(core)) {
    out.ofx = horizontalCenter(core) << 16;
  }
  return out;
}

int32_t guestX(Core *core, int32_t drawnX) {
  return drawnX - horizontalOffsetDelta(core);
}

bool guestOnScreenX(Core *core, int32_t drawnX) {
  const int32_t x = guestX(core, drawnX);
  return x > 0 && x < kGuestClipRight;
}

bool drawnOnScreenX(int drawRight, int32_t drawnX) {
  return drawnX > 0 && drawnX < drawRight;
}

} // namespace spyro::wide_screen_space
