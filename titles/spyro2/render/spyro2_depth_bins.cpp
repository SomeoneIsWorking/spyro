#include "spyro2_depth_bins.h"

#include "core.h"
#include "execution_control.h"
#include "lucent/log.h"
#include "native_dispatch.h"
#include "native_execution.h"
#include "ordering_table.h"
#include "spyro2_render_facts.h"
#include "terrain_packet_sink.h"

#include <algorithm>
#include <cstdlib>

namespace spyro2::depth_bins {
namespace {

namespace sink = spyro::terrain_packet_sink;

// The bin count the flatten asks for (800159A4 passes 0x580); its start clamps to the mark's bin.
constexpr std::int32_t kScanBins = 0x580;
constexpr std::uint32_t kMarkSlack = sink::kBinStride;

// A packet's tag: GP0 word count in the high byte, the next packet's RAM offset in the low 24
// bits, zero ending a bin's chain until the flatten links it. A bin's chain starts at the second
// word of its pair (the first packet linked) and ends at the head (terrain_packet_sink.h).
constexpr std::uint32_t kNextMask = 0x00FFFFFFu;

void flattenOverride(Core *core) {
  assignBuckets(*core, kRenderGlobals);
  (void)psx::cpu::completeOrPropagate(
      *core,
      psx::cpu::callOriginal(*core, kFlatten, psx::cpu::ExecutionBudget::currentTurn(*core)));
}

} // namespace

void assignBuckets(Core &core, const spyro::guest_render_globals::Globals &globals) {
  const std::uint32_t table = core.mem_r32(globals.orderingTable);
  core.otTables.name(kTable, table, sink::kBins, sink::kBinStride, psx::gpu::OtWalk::HighToLow);
  const std::int32_t reach = static_cast<std::int32_t>(
      (core.mem_r32(globals.orderingTableMark) + kMarkSlack - table) / sink::kBinStride);
  const std::int32_t bins = std::min(kScanBins, reach);
  std::uint32_t assigned = 0;
  for (std::int32_t bin = 0; bin < bins; ++bin) {
    std::uint32_t packet = core.mem_r32(table + static_cast<std::uint32_t>(bin) * sink::kBinStride +
                                        sink::kBinFirstOffset);
    while (packet != 0) {
      if (++assigned > static_cast<std::uint32_t>(psx::gpu::kOtNodeLimit)) {
        lucent::error("spyro2bins",
                      "refusing to assign more than {} packets in one frame: bin {} chains on past "
                      "the packet pool",
                      psx::gpu::kOtNodeLimit,
                      bin);
        std::abort();
      }
      core.otTables.assign(packet, psx::present::OtSlot{kTable, static_cast<std::uint32_t>(bin)});
      const std::uint32_t next = core.mem_r32(packet) & kNextMask;
      packet = next == 0 ? 0 : psx::gpu::guestAddressOf(next);
    }
  }
}

void registerOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, kFlatten, "spyro2-depth-bins", flattenOverride);
}

} // namespace spyro2::depth_bins
