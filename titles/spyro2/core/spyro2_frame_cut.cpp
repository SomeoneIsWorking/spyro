#include "spyro2_frame_cut.h"

#include "core.h"

#include <lucent/log.h>

namespace spyro2 {
namespace {

// The camera controller's modes that place the camera, and bit 7 of mode 9's sub-state, which
// FUN_8001FA58 keeps through the placement (0x80 -> 0x81).
inline constexpr std::uint32_t kModeSavedCamera = 9;
inline constexpr std::uint32_t kModeScripted = 10;
inline constexpr std::uint32_t kModeRespawn = 0xB;
inline constexpr std::uint32_t kScriptEnding = 0x80;
inline constexpr std::uint32_t kSubStateFlag = 0x80;

} // namespace

bool FrameCut::cameraPlaced(const CameraMachine &before, const CameraMachine &after) {
  if (before.mode != after.mode || before.subState == after.subState) {
    return false;
  }
  switch (before.mode) {
  case kModeSavedCamera:
    return (before.subState & ~kSubStateFlag) == 0 && (after.subState & ~kSubStateFlag) != 0;
  case kModeRespawn:
    return before.subState == 0;
  case kModeScripted:
    return before.subState == 0 || before.subState == kScriptEnding;
  default:
    return false;
  }
}

void FrameCut::onFrameDrawn(Core &core) {
  walked_ = SceneIdentity{
      .gameState = core.mem_r32(kGameState),
      .levelId = core.mem_r32(kLevelId),
  };
  walkedCamera_ = CameraMachine{
      .mode = core.mem_r32(kCameraMode),
      .subState = core.mem_r32(kCameraSubState),
  };
}

void FrameCut::onFrameTail(Core &) {
  // A record that walked no table is no scene change; the presenter never shows it.
  if (!walked_.has_value()) {
    cut_ = false;
    return;
  }
  cut_ = !sealed_.has_value() || *sealed_ != *walked_ || cameraPlaced(sealedCamera_, walkedCamera_);
  lucent::debug("cut",
                "cut={} state={} level={} camera={}/{}",
                cut_,
                walked_->gameState,
                walked_->levelId,
                walkedCamera_.mode,
                walkedCamera_.subState);
  sealed_ = walked_;
  sealedCamera_ = walkedCamera_;
  walked_.reset();
}

} // namespace spyro2
