#include "spyro2_recomp_register.h"

#include "core.h"
#include "overlay_table.h"
#include "override_registry.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro2_gpu_sync.h"

#include <cstdint>

extern void shard_set_override(std::uint32_t, void (*)(Core *));

namespace spyro2 {
namespace {

const RecompRegistry kRegistry{
    .main_dispatch = main_dispatch,
    .rec_func_index = rec_func_index,
    .overlays = g_rec_overlays,
    .overlay_count = g_rec_overlay_count,
    .shard_set_override = shard_set_override,
    .ov_a00_set_override = nullptr,
    .ov_game_set_override = nullptr,
    .guestMemset_gen = nullptr,
};

constexpr std::uint32_t kDrawSync = 0x800557E4u;
constexpr std::uint32_t kGpuTimeoutArm = 0x80057880u;
constexpr std::uint32_t kGpuTimeoutCheck = 0x800578B4u;

void drawSyncOverride(Core *core) {
  completeDrawSync(*core);
}

void gpuTimeoutArmOverride(Core *core) {
  armGpuTimeout(*core);
}

void gpuTimeoutCheckOverride(Core *core) {
  checkGpuTimeout(*core);
}

} // namespace

void installRecompSubstrate() {
  psxport_install_recomp(&kRegistry);
  overrides::install(
      kDrawSync, "Spyro2::DrawSync", drawSyncOverride, gen_func_800557E4, shard_set_override);
  overrides::install(kGpuTimeoutArm,
                     "Spyro2::GpuTimeoutArm",
                     gpuTimeoutArmOverride,
                     gen_func_80057880,
                     shard_set_override);
  overrides::install(kGpuTimeoutCheck,
                     "Spyro2::GpuTimeoutCheck",
                     gpuTimeoutCheckOverride,
                     gen_func_800578B4,
                     shard_set_override);
}

} // namespace spyro2
