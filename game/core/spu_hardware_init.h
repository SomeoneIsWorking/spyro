// Binary-derived reset policy for InitSpuHardware (0x8005BBF4).
#pragma once

#include <cstdint>

namespace spyro {

struct SpuVoiceReset {
  uint16_t volumeLeft;
  uint16_t volumeRight;
  uint16_t pitch;
  uint16_t startAddress;
  uint16_t adsr1;
  uint16_t adsr2;
};

constexpr uint32_t kSpuVoiceCount = 24u;
constexpr uint32_t kSpuPendingPitchCount = 10u;

constexpr bool spuHardwareNeedsFullReset(uint32_t mode) {
  return mode == 0u;
}

constexpr SpuVoiceReset spuVoiceReset() {
  return {
      .volumeLeft = 0u,
      .volumeRight = 0u,
      .pitch = 0x3FFFu,
      .startAddress = 0x200u,
      .adsr1 = 0u,
      .adsr2 = 0u,
  };
}

} // namespace spyro
