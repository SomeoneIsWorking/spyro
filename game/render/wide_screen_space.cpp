#include "wide_screen_space.h"

#include "core.h"
#include "gpu_vk.h"

namespace spyro::wide_screen_space {
namespace {

// The horizontal offset the port projected with, minus the one the guest's own projection uses.
// Zero unless the wide engine widened the window.
int32_t horizontalOffsetDelta(Core *core, int drawRight) {
  if (!gpu_vk_wide_engine(core)) {
    return 0;
  }
  return horizontalCenter(core) - (int32_t)core->rsub.projParams.geomOfx();
}

} // namespace

int32_t horizontalCenter(Core *core) {
  if (gpu_vk_wide_engine(core)) {
    return (int32_t)(gpu_vk_wide_engine_w(core) / 2);
  }
  return (int32_t)core->rsub.projParams.geomOfx();
}

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
    out.ofx = horizontalCenter(core) << 16;
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

} // namespace spyro::wide_screen_space
