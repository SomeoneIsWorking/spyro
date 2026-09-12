#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class Core;

namespace spyro1 {

struct CameraProjectionSample {
  std::uint32_t candidates = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t depth = 0;
  std::uint32_t targetState = 0;
  std::uint32_t cameraMode = 0;
  std::uint32_t lookMode = 0;
  std::uint32_t playerCameraGate = 0;
  std::uint32_t cameraBlock = 0;
};

// Diagnostic record at the completed outer StageUpdate return. This does not observe nested camera
// returns; its nested GTE tap captures the exact projection within that outer call.
struct StageUpdateSample {
  std::uint32_t stage = 0;
  std::uint32_t levelTick = 0;
  std::uint32_t gameTick = 0;
  std::uint32_t cameraState = 0;
  std::uint32_t cameraTargetState = 0;
  std::uint32_t cameraMode = 0;
  std::uint32_t lookMode = 0;
  std::uint32_t playerCameraGate = 0;
  std::uint32_t cameraBlock = 0;
  std::uint32_t cameraEntranceTimer = 0;
  std::uint64_t gteOps = 0;
  std::uint32_t rtpsOps = 0;
  CameraProjectionSample projection{};
  std::array<std::int32_t, 3> player{};
  std::array<std::int32_t, 3> camera{};
};

class StageUpdateObserver {
public:
  StageUpdateObserver(bool enabled, std::uint32_t target);

  void beginStage(Core &core, std::uint32_t entry);
  void afterReturn(Core &core, std::uint32_t entry);
  void report() const;

  std::uint64_t scanned() const {
    return scanned_;
  }
  std::uint64_t matched() const {
    return matched_;
  }
  std::uint64_t gameplay() const {
    return gameplay_;
  }
  std::span<const StageUpdateSample> samples() const {
    return {samples_.data(), used_};
  }

private:
  static constexpr std::size_t kFirstGameplaySamples = 128;
  static void beforeGte(Core *core,
                        std::uint64_t ordinal,
                        std::uint32_t guestPc,
                        std::uint32_t instruction,
                        void *user);
  static void afterGte(Core *core,
                       std::uint64_t ordinal,
                       std::uint32_t guestPc,
                       std::uint32_t instruction,
                       void *user);

  bool enabled_ = false;
  bool stageArmed_ = false;
  std::uint32_t target_ = 0;
  std::uint64_t scanned_ = 0;
  std::uint64_t matched_ = 0;
  std::uint64_t gameplay_ = 0;
  std::uint64_t gteOps_ = 0;
  std::uint64_t rtpsOps_ = 0;
  std::uint64_t projectionCandidates_ = 0;
  std::uint64_t uniqueProjections_ = 0;
  std::array<StageUpdateSample, kFirstGameplaySamples> samples_{};
  std::size_t used_ = 0;
  std::uint32_t stageRtpsOps_ = 0;
  std::uint64_t pendingProjectionOrdinal_ = 0;
  CameraProjectionSample stageProjection_{};
  std::uint64_t stageGteOps_ = 0;
};

} // namespace spyro1
