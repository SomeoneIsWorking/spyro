#include "core.h"
#include "game.h"
#include "game_iface.h"
#include "game_runtime.h"
#include "spyro2_gpu_sync.h"

#include <array>
#include <memory>

namespace {

constexpr std::uint32_t kTimeoutDeadline = 0x80066354u;
constexpr std::uint32_t kTimeoutPollCount = 0x80066358u;

class SyncRuntime final : public GameRuntime {
public:
  void *createContext(Core &) override {
    return nullptr;
  }
  void destroyContext(void *) override {}
  void registerOverrides(Game &) override {}
  void bootInit(Core &) override {}
  RenderCapabilities renderCapabilities() const override {
    return RenderCapabilities::direct();
  }
  bool guestVramIsPicture(const Game &) const override {
    return true;
  }
};

} // namespace

int main() {
  SyncRuntime runtime;
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  Core &core = game->core;

  std::array<std::uint32_t, 32> before{};
  for (std::uint32_t index = 0; index < before.size(); ++index) {
    core.r[index] = 0xA5000000u + index;
    before[index] = core.r[index];
  }
  const std::uint64_t fenceBefore = game->presentation.fence();
  const std::uint32_t fieldBefore = game->timing.vblank;

  spyro2::completeDrawSync(core);

  if (core.r[2] != 0u || game->presentation.fence() != fenceBefore ||
      game->timing.vblank != fieldBefore) {
    return 1;
  }
  for (std::uint32_t index = 0; index < before.size(); ++index) {
    if (index != 2u && core.r[index] != before[index]) {
      return 1;
    }
  }

  core.mem_w32(kTimeoutDeadline, 0u);
  core.mem_w32(kTimeoutPollCount, 42u);
  core.r[2] = 0xDEADBEEFu;
  spyro2::armGpuTimeout(core);
  if (core.r[2] != 0xDEADBEEFu || core.mem_r32(kTimeoutDeadline) != 0x7FFFFFFFu ||
      core.mem_r32(kTimeoutPollCount) != 0u || game->presentation.fence() != fenceBefore ||
      game->timing.vblank != fieldBefore) {
    return 1;
  }

  spyro2::checkGpuTimeout(core);
  if (core.r[2] != 0u || game->presentation.fence() != fenceBefore ||
      game->timing.vblank != fieldBefore) {
    return 1;
  }
  return 0;
}
