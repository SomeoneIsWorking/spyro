// Whether a sealed Spyro 2 record is a cut, for the 60 fps in-between.
//
// A record is a cut when its scene is not the previous record's: another game state (0x800681C8,
// the update's switch operand at 0x8001B164) or another level (0x80066F90, read at 0x80053D50).
// The scene is sampled when the draw 0x800156FC returns, right after its DrawOTag walked the
// table, because the draw that walks a table often resumes a step after the one that built it.
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

  std::optional<SceneIdentity> walked_; // the last table walked this step
  std::optional<SceneIdentity> sealed_; // the last table a sealed record holds
  bool cut_ = true;
};

} // namespace spyro2
