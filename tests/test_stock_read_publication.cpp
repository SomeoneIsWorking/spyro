// Spyro 2 and Spyro 3 stream code modules through the framework's stock CdRead and then call into
// them. Every case drives the SHIPPING path -- `cd_read_stock_sync`, the framework's landing
// announcement, the title runtime's override, the image catalog, and real Lightrec dispatch into
// the loaded bytes -- with fake sectors standing in only for the disc.
//
// The positive case executes the loaded module. The negatives are the ones a green run cannot tell
// from a working one: a read that landed nothing, a failed read, an unpositioned drive, and a
// landing that does not fit main RAM must each publish NOTHING, and a replacement read at the same
// address must mint a new generation so the previous module stops resolving.

#include "cd_control.h"
#include "cd_stock_read_completion.h"
#include "cdc_state.h"
#include "core.h"
#include "execution_control.h"
#include "execution_exit.h"
#include "game.h"
#include "image_identity.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "spyro2_runtime.h"
#include "spyro3_runtime.h"
#include "spyro_context.h"
#include "testutil.h"

#include <array>
#include <cstdint>
#include <memory>

namespace {

constexpr std::uint32_t kModule = 0x8006D264u; // Spyro 2's measured load address (the heap base)
constexpr std::uint32_t kStopHere = 0x800F0000u;
constexpr std::uint32_t kModuleLba = 570u;

// The value the next sector stream's first instruction loads into $v0. A sector's words are
// `addiu $v0,$zero,value ; jr $ra ; nop`, then the sector position as filler, so two loads differ
// in both the executed result and their SHA-256.
std::uint32_t gValue = 7u;

int fakeSector(DiscState *, std::uint32_t lba, std::uint8_t *out, std::uint32_t) {
  for (std::uint32_t i = 0; i < 2352u; ++i) {
    out[i] = static_cast<std::uint8_t>(lba + i);
  }
  if (lba == kModuleLba) {
    const std::array<std::uint32_t, 3> instructions{0x24020000u | gValue, 0x03E00008u, 0u};
    for (std::uint32_t word = 0; word < instructions.size(); ++word) {
      for (std::uint32_t byte = 0; byte < 4u; ++byte) {
        out[24u + word * 4u + byte] = static_cast<std::uint8_t>(instructions[word] >> (byte * 8u));
      }
    }
  }
  return 1;
}

struct Machine {
  explicit Machine(GameRuntime &runtime) : game(std::make_unique<Game>()) {
    game->core.gameCtx = &context;
    game->runtime = &runtime;
    game->gpu_dev.s_gpu_on = 0;
    game->core.r[29] = 0x801FFF00u;
    game->core.r[30] = 0x801FFF00u;
    game->cdc.disc_read_raw_fn = fakeSector;
  }

  bool read(std::uint32_t sectors, std::uint32_t destination, std::int32_t lba) {
    game->cd.setloc_lba = lba;
    game->core.r[4] = sectors;
    game->core.r[5] = destination;
    game->core.r[6] = 0x80u;
    cd_read_stock_sync(&game->core);
    return game->core.r[2] == 1u;
  }

  // $v0 after the loaded module returns; 0xFFFFFFFF when it did not return through the JIT.
  std::uint32_t run() {
    Core &core = game->core;
    core.r[31] = kStopHere;
    const auto result =
        psx::cpu::dispatchGuest(core, kModule, psx::cpu::ExecutionBudget::fromCycles(4096u));
    return result.returned() ? core.r[2] : 0xFFFFFFFFu;
  }

  SpyroContext context;
  std::unique_ptr<Game> game;
};

template <typename Runtime> void checkTitle() {
  Runtime runtime;
  Machine machine(runtime);
  Core &core = machine.game->core;
  auto &catalog = core.imageCatalog();

  // Nothing is published before any read: the dispatch of this address is the typed fault the boot
  // hit.
  CHECK(!core.currentImageIdentity(kModule).has_value());
  const auto baseline = catalog.activeCount();

  // POSITIVE: a 2-sector module lands, is published, resolves, and EXECUTES through Lightrec.
  gValue = 7u;
  CHECK(machine.read(2u, kModule, static_cast<std::int32_t>(kModuleLba)));
  const auto first = core.currentImageIdentity(kModule);
  CHECK(first.has_value());
  CHECK(core.currentImageIdentity(kModule + 2u * 2048u - 1u) == first);
  CHECK(!core.currentImageIdentity(kModule + 2u * 2048u).has_value()); // exactly the landed bytes
  CHECK_EQ(catalog.activeCount(), baseline + 1u);
  const auto described = catalog.describe(*first);
  CHECK(described.has_value());
  CHECK(described->name.starts_with("CD read SHA-256 "));
  CHECK_EQ(machine.run(), 7u);
  CHECK(core.lightrecExecutor().counters().executedBlocks != 0u);

  // REPLACEMENT: the same address with different bytes is a new generation; the old one is gone.
  gValue = 11u;
  CHECK(machine.read(2u, kModule, static_cast<std::int32_t>(kModuleLba)));
  const auto second = core.currentImageIdentity(kModule);
  CHECK(second.has_value());
  CHECK(*second != *first);
  CHECK(catalog.describe(*second)->contentIdentity != described->contentIdentity);
  CHECK_EQ(machine.run(), 11u);

  // A reload of IDENTICAL bytes is still a replacement: same content identity, new generation.
  CHECK(machine.read(2u, kModule, static_cast<std::int32_t>(kModuleLba)));
  const auto third = core.currentImageIdentity(kModule);
  CHECK(third.has_value() && *third != *second);
  CHECK(catalog.describe(*third)->contentIdentity == catalog.describe(*second)->contentIdentity);
  const auto published = catalog.activeCount();

  // NEGATIVE: a read that moved no sector, an unpositioned drive, and a failing source publish
  // nothing.
  CHECK(machine.read(0u, kModule + 0x4000u, static_cast<std::int32_t>(kModuleLba)));
  CHECK(!core.currentImageIdentity(kModule + 0x4000u).has_value());
  CHECK(!machine.read(1u, kModule + 0x4000u, -1));
  CHECK(!core.currentImageIdentity(kModule + 0x4000u).has_value());
  machine.game->cdc.disc_read_raw_fn =
      [](DiscState *, std::uint32_t, std::uint8_t *, std::uint32_t) -> int {
    return 0;
  };
  CHECK(!machine.read(1u, kModule + 0x4000u, static_cast<std::int32_t>(kModuleLba)));
  CHECK(!core.currentImageIdentity(kModule + 0x4000u).has_value());
  CHECK_EQ(catalog.activeCount(), published);
  CHECK(!core.executionControl().consume().has_value()); // none of those is a fault
}

// A landing the title cannot publish is a runtime fault, never a silent run under no identity. The
// framework would not announce one outside RAM (its own writes refuse first), so this drives the
// title's owner directly with the exact range the framework would have named.
void checkUnpublishableLanding() {
  spyro2::Spyro2Runtime runtime;
  Machine machine(runtime);
  Core &core = machine.game->core;
  const auto baseline = core.imageCatalog().activeCount();

  runtime.stockCdReadLanded(
      core, {.firstLba = 1u, .sectors = 1u, .destination = 0x801FFC00u, .bytes = 2048u});
  const auto fault = core.executionControl().consume();
  CHECK(fault.has_value());
  CHECK(fault->reason == psx::cpu::ExecutionExitReason::Fault);
  CHECK_EQ(core.imageCatalog().activeCount(), baseline);

  runtime.stockCdReadLanded(core,
                            {.firstLba = 1u, .sectors = 0u, .destination = kModule, .bytes = 0u});
  CHECK(core.executionControl().consume().has_value());
  CHECK_EQ(core.imageCatalog().activeCount(), baseline);
}

void test_spyro2_publishes_a_stock_read_module() {
  checkTitle<spyro2::Spyro2Runtime>();
}

void test_spyro3_publishes_a_stock_read_module() {
  checkTitle<spyro3::Spyro3Runtime>();
}

void test_an_unpublishable_landing_is_a_runtime_fault() {
  checkUnpublishableLanding();
}

} // namespace

int main() {
  RUN(spyro2_publishes_a_stock_read_module);
  RUN(spyro3_publishes_a_stock_read_module);
  RUN(an_unpublishable_landing_is_a_runtime_fault);
  return pt_summary();
}
