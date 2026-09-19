#include "particle_screen_space.h"

#include "core.h"
#include "gpu_vk.h"

namespace spyro::particle_screen_space {
namespace {

// The horizontal offset the port projected with, minus the one the guest's own projection uses.
// Zero unless the wide engine widened the window.
int32_t horizontalOffsetDelta(Core *core, int drawRight) {
  if (!gpu_vk_wide_engine(core)) {
    return 0;
  }
  return (int32_t)(drawRight / 2) - (int32_t)core->rsub.projParams.geomOfx();
}

} // namespace

int drawClipRight(Core *core) {
  if (gpu_vk_wide_engine(core)) {
    return gpu_vk_wide_engine_w(core);
  }
  return kGuestClipRight;
}

psxport::native_projection::ProjectionParams projection(Core *core, int drawRight) {
  psxport::native_projection::ProjectionParams out{};
  out.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  out.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  out.h = (uint16_t)core->rsub.projParams.geomH();
  if (gpu_vk_wide_engine(core)) {
    out.ofx = (drawRight / 2) << 16;
  }
  return out;
}

bool guestOnScreenX(Core *core, int drawRight, int32_t drawnX) {
  const int32_t guestX = drawnX - horizontalOffsetDelta(core, drawRight);
  return guestX > 0 && guestX < kGuestClipRight;
}

bool drawnOnScreenX(int drawRight, int32_t drawnX) {
  return drawnX > 0 && drawnX < drawRight;
}

} // namespace spyro::particle_screen_space
