// Whether a sealed Spyro 2 record is a cut, for the 60 fps in-between.
//
// A record is a cut when its scene is not the previous record's: another game state (0x800681C8,
// the update's switch operand at 0x8001B164) or another level (0x80066F90, read at 0x80053D50), or
// when the camera was re-placed inside one scene. The camera controller FUN_8001FA58 places the
// camera only at three steps of its mode machine (mode 0x80067ED0, sub-state 0x80067ED4), each
// after the guest's own full-screen fade:
//   mode 9    sub-state 0 -> 1 (bit 7 aside): the saved camera 0x80067FF4 / target 0x80068008
//   mode 0xB  sub-state 0 -> 1: the player and camera at the respawn position 0x8006A21C
//   mode 10   sub-state 0 -> 1: the scripted camera begins (0x80067FC9 set), and
//             sub-state 0x80 -> 0x81: it ends and the gameplay camera returns
// Every other mode change keeps the camera where it was (FUN_800216D8 re-derives the follow angles
// from the camera's own position). The scene and the camera are sampled when the draw 0x800156FC
// returns, right after its DrawOTag walked the table, because the draw that walks a table often
// resumes a step after the one that built it.
#pragma once

#include "field_owner.h"

#include <cstdint>
#include <optional>

class Core;

namespace spyro2 {

class FrameCut final : public spyro::FrameTailObserver {
public:
  inline static constexpr std::uint32_t kGameState = 0x800681C8u;
  inline static constexpr std::uint32_t kLevelId = 0x80066F90u;
  inline static constexpr std::uint32_t kCameraMode = 0x80067ED0u;
  inline static constexpr std::uint32_t kCameraSubState = 0x80067ED4u;

  void onFrameDrawn(Core &core) override;
  void onFrameTail(Core &core) override;
  // Whether the record sealed after the latest frame tail is a cut from the one before it.
  [[nodiscard]] bool isCut() const {
    return cut_;
  }

private:
  struct SceneIdentity {
    std::uint32_t gameState = 0;
    std::uint32_t levelId = 0;
    bool operator==(const SceneIdentity &) const = default;
  };

  struct CameraMachine {
    std::uint32_t mode = 0;
    std::uint32_t subState = 0;
  };

  // Whether the camera controller placed the camera between two samples of its mode machine.
  [[nodiscard]] static bool cameraPlaced(const CameraMachine &before, const CameraMachine &after);

  std::optional<SceneIdentity> walked_; // the last table walked this step
  std::optional<SceneIdentity> sealed_; // the last table a sealed record holds
  CameraMachine walkedCamera_;
  CameraMachine sealedCamera_;
  bool cut_ = true;
};

} // namespace spyro2
