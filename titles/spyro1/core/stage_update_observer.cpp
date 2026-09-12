#include "stage_update_observer.h"

#include "core.h"

#include <lucent/log.h>

namespace spyro1 {
namespace {

// Addresses and offsets from the authenticated Spyro 1 executable and external/spyro-1 symbols.
constexpr std::uint32_t kStage = 0x800757D8u;
constexpr std::uint32_t kLevelTick = 0x800758C8u;
constexpr std::uint32_t kGameTick = 0x8007572Cu;
constexpr std::uint32_t kCamera = 0x80076DD0u;
constexpr std::uint32_t kCameraPosition = kCamera + 0x28u;
constexpr std::uint32_t kCameraState = kCamera + 0x58u;
constexpr std::uint32_t kCameraTargetState = kCamera + 0xC0u;
constexpr std::uint32_t kSpyroPosition = 0x80078A58u;

std::array<std::int32_t, 3> position(Core &core, std::uint32_t address) {
  return {static_cast<std::int32_t>(core.mem_r32(address)),
          static_cast<std::int32_t>(core.mem_r32(address + 4u)),
          static_cast<std::int32_t>(core.mem_r32(address + 8u))};
}

} // namespace

StageUpdateObserver::StageUpdateObserver(bool enabled, std::uint32_t target)
    : enabled_(enabled), target_(target) {}

void StageUpdateObserver::afterReturn(Core &core, std::uint32_t entry) {
  if (!enabled_) {
    return;
  }
  ++scanned_;
  if (entry != target_) {
    return;
  }
  ++matched_;
  if (core.mem_r32(kStage) != 0u) {
    return;
  }
  ++gameplay_;
  if (used_ == samples_.size()) {
    return;
  }
  samples_[used_++] = {
      .stage = 0u,
      .levelTick = core.mem_r32(kLevelTick),
      .gameTick = core.mem_r32(kGameTick),
      .cameraState = core.mem_r32(kCameraState),
      .cameraTargetState = core.mem_r32(kCameraTargetState),
      .player = position(core, kSpyroPosition),
      .camera = position(core, kCameraPosition),
  };
}

void StageUpdateObserver::report() const {
  if (!enabled_) {
    return;
  }
  lucent::info(
      "stage-observe",
      "completed-return target=0x{:08X} scanned={} matched={} gameplay={} recorded={} omitted={}",
      target_,
      scanned_,
      matched_,
      gameplay_,
      used_,
      gameplay_ - used_);
  for (std::size_t index = 0; index < used_; ++index) {
    const StageUpdateSample &sample = samples_[index];
    lucent::info("stage-observe",
                 "return={} stage={} level_tick={} game_tick={} camera_state=0x{:08X} "
                 "camera_target_state=0x{:08X} player=({},{},{}) camera=({},{},{})",
                 index,
                 sample.stage,
                 sample.levelTick,
                 sample.gameTick,
                 sample.cameraState,
                 sample.cameraTargetState,
                 sample.player[0],
                 sample.player[1],
                 sample.player[2],
                 sample.camera[0],
                 sample.camera[1],
                 sample.camera[2]);
  }
}

} // namespace spyro1
