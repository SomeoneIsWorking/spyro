#include "spyro2_gpu_sync.h"

#include "core.h"

namespace spyro2 {
namespace {

constexpr std::uint32_t kTimeoutDeadline = 0x80066354u;
constexpr std::uint32_t kTimeoutPollCount = 0x80066358u;

} // namespace

void completeDrawSync(Core &core) {
  // The host GPU consumes guest GP0/DMA work synchronously. DrawSync(0) therefore has no pending
  // work and returns success without spending a display field or entering its retail timeout loop.
  core.r[2] = 0u;
}

void armGpuTimeout(Core &core) {
  // Retail derives this deadline from VSync(-1). The host GPU is synchronous, so it can never reach
  // the display-clock timeout: use the same far-future deadline as the framework's synchronous GPU
  // owner and reset the measured poll-count global without querying or advancing a field.
  core.mem_w32(kTimeoutDeadline, 0x7FFFFFFFu);
  core.mem_w32(kTimeoutPollCount, 0u);
}

void checkGpuTimeout(Core &core) {
  completeDrawSync(core);
}

} // namespace spyro2
