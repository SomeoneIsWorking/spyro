#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class Core;

namespace spyro1 {

// Diagnostic record at the completed outer StageUpdate return. This does not observe nested camera
// calls; it provides a fixed phase boundary for comparison with the console's matching return.
struct StageUpdateSample {
  std::uint32_t stage = 0;
  std::uint32_t levelTick = 0;
  std::uint32_t gameTick = 0;
  std::uint32_t cameraState = 0;
  std::uint32_t cameraTargetState = 0;
  std::array<std::int32_t, 3> player{};
  std::array<std::int32_t, 3> camera{};
};

class StageUpdateObserver {
public:
  StageUpdateObserver(bool enabled, std::uint32_t target);

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

  bool enabled_ = false;
  std::uint32_t target_ = 0;
  std::uint64_t scanned_ = 0;
  std::uint64_t matched_ = 0;
  std::uint64_t gameplay_ = 0;
  std::array<StageUpdateSample, kFirstGameplaySamples> samples_{};
  std::size_t used_ = 0;
};

} // namespace spyro1
