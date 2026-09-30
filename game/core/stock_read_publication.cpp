#include "stock_read_publication.h"

#include "core.h"
#include "execution_control.h"
#include "image_publication.h"

#include <lucent/log.h>
#include <span>
#include <string>

namespace spyro {
namespace {

void refuse(Core &core, const psx::cd::StockReadLanding &landing, const char *detail) {
  psx::cpu::requestExecutionExit(
      core,
      {.reason = psx::cpu::ExecutionExitReason::Fault,
       .guestPc = core.pc,
       .detail = std::string("Spyro stock CdRead landing refused: ") + detail});
  lucent::error("cd",
                "stock CdRead landing of {} byte(s) at 0x{:08X} from LBA {} was NOT published as "
                "an image: {}",
                landing.bytes,
                landing.destination,
                landing.firstLba,
                detail);
}

} // namespace

void publishStockReadLanding(Core &core, const psx::cd::StockReadLanding &landing) {
  const auto begin = landing.destination & 0x1fffffffu;
  if (landing.bytes == 0u || begin >= sizeof core.ram || landing.bytes > sizeof core.ram - begin) {
    refuse(core, landing, "the landed range is empty or does not fit guest main RAM");
    return;
  }
  const auto content =
      image_publication::digest(std::span<const std::uint8_t>(core.ram + begin, landing.bytes));
  if (!content) {
    refuse(core, landing, "SHA-256 calculation failed");
    return;
  }
  const auto identity =
      image_publication::activate(core, "CD read", {begin, begin + landing.bytes}, *content);
  lucent::debug("cd",
                "stock CdRead of {} sector(s) from LBA {} published as image {} generation {}: "
                "0x{:08X}..0x{:08X}, "
                "SHA-256 {}",
                landing.sectors,
                landing.firstLba,
                identity.id,
                identity.generation,
                landing.destination,
                landing.destination + landing.bytes,
                content->hex);
}

} // namespace spyro
