#pragma once

#include "render.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class Core;

namespace spyro1 {

struct SpriteQueueOffsetSample {
  std::uint64_t ordinal = 0;
  std::uint32_t stage = 0;
  std::uint32_t gameTick = 0;
  std::uint32_t actor = 0;
  std::uint32_t actorWrites = 0;
  spyro::render::GteOffsetSample entry{};
  spyro::render::GteOffsetSample actorBefore{};
  spyro::render::GteOffsetSample actorAfter{};
  spyro::render::GteOffsetSample exit{};
};

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
  spyro::render::GteOffsetSample offset{};
  std::uint32_t h = 0;
  SpriteQueueOffsetSample precedingQueue{};
  std::uint64_t precedingActorWrites = 0;
  std::uint64_t precedingOfx100Writes = 0;
  std::uint64_t precedingSentinelHits = 0;
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

class StageUpdateObserver final : public spyro::render::SpriteQueueOffsetObserver {
public:
  StageUpdateObserver(bool enabled, std::uint32_t target);

  void beginStage(Core &core, std::uint32_t entry);
  void afterReturn(Core &core, std::uint32_t entry);
  void beginSpriteQueue(Core &core, spyro::render::GteOffsetSample entry) override;
  void spriteActorWrite(std::uint32_t actor,
                        spyro::render::GteOffsetSample before,
                        spyro::render::GteOffsetSample after) override;
  void endSpriteQueue(spyro::render::GteOffsetSample exit) override;
  void report() const;

  bool enabled() const {
    return enabled_;
  }
  std::uint64_t queueCalls() const {
    return queueCalls_;
  }
  std::uint64_t actorWrites() const {
    return actorWrites_;
  }
  std::uint64_t ofx100Writes() const {
    return ofx100Writes_;
  }
  std::uint64_t sentinelHits() const {
    return sentinelHits_;
  }
  const SpriteQueueOffsetSample &lastQueue() const {
    return lastQueue_;
  }

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
  std::uint64_t queueCalls_ = 0;
  std::uint64_t queueExits_ = 0;
  std::uint64_t actorWrites_ = 0;
  std::uint64_t ofx100Writes_ = 0;
  std::uint64_t sentinelHits_ = 0;
  bool queueActive_ = false;
  SpriteQueueOffsetSample activeQueue_{};
  SpriteQueueOffsetSample lastQueue_{};
};

} // namespace spyro1
