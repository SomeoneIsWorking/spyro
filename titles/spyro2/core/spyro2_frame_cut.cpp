#include "spyro2_frame_cut.h"

#include "core.h"

#include <lucent/log.h>

namespace spyro2 {

void FrameCut::onFrameDrawn(Core &core) {
  walked_ = SceneIdentity{
      .gameState = core.mem_r32(kGameState),
      .levelId = core.mem_r32(kLevelId),
  };
}

void FrameCut::onFrameTail(Core &) {
  // A record that walked no table is no scene change; the presenter never shows it.
  if (!walked_.has_value()) {
    cut_ = false;
    return;
  }
  cut_ = !sealed_.has_value() || *sealed_ != *walked_;
  lucent::debug("cut", "cut={} state={} level={}", cut_, walked_->gameState, walked_->levelId);
  sealed_ = walked_;
  walked_.reset();
}

} // namespace spyro2
