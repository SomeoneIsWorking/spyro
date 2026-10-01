// Each title's measured boot-prefix facts, and the runtime plan that has to agree with them.
//
// The shared driver is tested over synthetic facts in `test_boot_prefix_frame_driver.cpp`. What
// that cannot see is a wrong guest address in a title's own facts, which shows up only in a run
// with a disc attached: a driver that entered the wrong boot prefix, or a plan that names a leaf
// its own windows do not admit (the framework refuses it at registration with "no direct-runtime
// hardware-service window admits 0x...", which in a real run reads as a boot that stops for an
// unrelated reason).
//
// The literal addresses below are deliberately a SECOND copy of what `spyro2_boot_facts.h` and
// `spyro3_boot_facts.h` declare, each quoted from that title's executable: a test that read them
// back from the facts would pass for any value. The runtimes are asked for their driver, so a title
// whose runtime composed the other title's facts fails here too.

#include "boot_prefix_frame_driver.h"
#include "core.h"
#include "game.h"
#include "platform_hle.h"
#include "spyro2_runtime.h"
#include "spyro3_runtime.h"
#include "spyro_context.h"
#include "testutil.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>

namespace {

struct ExpectedTitle {
  const char *name;
  std::uint32_t bootPrefix;
  std::uint32_t frameUpdate;
  std::uint32_t frameDraw;
  std::uint32_t vsync;
  // The guest vblank word retail VSync(-1) returns (libetc's callback increments it); never the
  // HBlank root counter 0x1F801110, which wraps and stalls the title's frame limiter (issue 0160).
  std::uint32_t vsyncQueryCounter;
  // libcd CdInit: retail runs it so libcd registers its interrupt handler (issue 0159).
  std::uint32_t cdInit;
  // libcd CdControlB, bound to the synchronous owner when the title's body would wait out libcd's
  // deadline (issue 0161); zero when the title does not bind it.
  std::uint32_t boundCdControlB;
};

constexpr ExpectedTitle kSpyro2{"Spyro 2", 0x80011E9Cu, 0x8001B140u, 0x800156FCu, 0x80058EDCu,
                                     0x80066618u, 0x800582B8u, 0x80058994u};
constexpr ExpectedTitle kSpyro3{"Spyro 3", 0x8002AB38u, 0x80055400u, 0x8001E638u, 0x8005956Cu,
                                     0x8006B480u, 0x8005DB1Cu, 0u};

// Half-open, and a zero high disables the slot, exactly as the framework reads the plan.
bool windowAdmits(const PlatformHlePlan &plan, std::uint32_t address) {
  for (int slot = 0; slot < kPlatformHleWindowCapacity; ++slot) {
    if (plan.windowHi[slot] != 0u && address >= plan.windowLo[slot] &&
        address < plan.windowHi[slot]) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<Game> newGame(SpyroContext &context, GameRuntime &runtime) {
  std::unique_ptr<Game> game = std::make_unique<Game>();
  game->core.gameCtx = &context;
  game->runtime = &runtime;
  return game;
}

void checkDriverFacts(GameRuntime &runtime, const ExpectedTitle &expected) {
  SpyroContext context;
  std::unique_ptr<Game> game = newGame(context, runtime);
  std::unique_ptr<FrameDriver> created = runtime.createFrameDriver(*game);
  const auto *driver = dynamic_cast<const spyro::BootPrefixFrameDriver *>(created.get());
  CHECK(driver != nullptr);
  if (driver == nullptr) {
    return;
  }
  const spyro::BootPrefixFacts &facts = driver->facts();
  CHECK(std::string_view(facts.titleName) == expected.name);
  CHECK(std::string_view(facts.field.titleName) == expected.name);
  CHECK_EQ(facts.bootPrefix, expected.bootPrefix);
  CHECK_EQ(facts.frameUpdate, expected.frameUpdate);
  CHECK_EQ(facts.frameDraw, expected.frameDraw);
  // Neither title overrides a limit: both measured boots return inside the shared defaults.
  CHECK_EQ(facts.bootStepFieldLimit, spyro::kDefaultBootStepFieldLimit);
  CHECK_EQ(facts.bootStepLimit, spyro::kDefaultBootStepLimit);
  CHECK(!driver->bootComplete());
  CHECK(!driver->bootStalled());
}

// Every address the runtime's measured plan declares lies inside one of the plan's own windows, and
// the plan declares the VSync this title's boot waits on.
void checkPlan(const GameRuntime &runtime, const ExpectedTitle &expected) {
  const PlatformHlePlan *plan = runtime.platformHlePlan();
  CHECK(plan != nullptr);
  if (plan == nullptr) {
    return;
  }
  CHECK_EQ(plan->vsyncAddress, expected.vsync);
  CHECK_EQ(plan->vsyncQueryCounterAddress, expected.vsyncQueryCounter);

  struct Declared {
    const char *name;
    std::uint32_t address;
  };
  // A zero address means "not located", and the framework installs nothing for it. Listing only the
  // ones a title claims is the point: the check is over the CLAIM, not over the array.
  const Declared declared[] = {
      {"setGeomOffset", plan->setGeomOffset},
      {"setGeomScreen", plan->setGeomScreen},
      {"cdRead", plan->cdReadAddress},
      {"cdReadSync", plan->cdReadSyncAddress},
      {"cdCommand", plan->cdCommandAddress},
      {"cdSync", plan->cdSyncAddress},
      {"cdSearchFile", plan->cdSearchFileAddress},
      {"drawSync", plan->drawSyncAddress},
      {"vsync", plan->vsyncAddress},
  };
  for (const Declared &leaf : declared) {
    if (leaf.address == 0u) {
      continue;
    }
    const bool admitted = windowAdmits(*plan, leaf.address);
    if (!admitted) {
      std::fprintf(
          stderr,
          "REFUSED: %s measured leaf %s 0x%08X lies outside every window the plan declares\n",
          expected.name,
          leaf.name,
          leaf.address);
    }
    CHECK(admitted);
  }
  CHECK(plan->bindingCount >= 0);
  CHECK(plan->bindingCount <= PlatformHlePlan::kMaxBindings);
  bool boundCdControlB = false;
  for (int index = 0; index < plan->bindingCount; ++index) {
    CHECK(plan->bindings[index].addr != expected.cdInit);
    boundCdControlB = boundCdControlB || plan->bindings[index].addr == expected.boundCdControlB;
  }
  CHECK_EQ(boundCdControlB, expected.boundCdControlB != 0u);
  for (int index = 0; index < plan->bindingCount; ++index) {
    const bool admitted = windowAdmits(*plan, plan->bindings[index].addr);
    if (!admitted) {
      std::fprintf(stderr,
                   "REFUSED: %s measured binding %d at 0x%08X lies outside every window\n",
                   expected.name,
                   index,
                   plan->bindings[index].addr);
    }
    CHECK(admitted);
    CHECK(plan->bindings[index].fn != nullptr);
  }
}

void test_spyro2_facts_and_plan_match_its_executable() {
  spyro2::Spyro2Runtime runtime;
  checkDriverFacts(runtime, kSpyro2);
  checkPlan(runtime, kSpyro2);
}

void test_spyro3_facts_and_plan_match_its_executable() {
  spyro3::Spyro3Runtime runtime;
  checkDriverFacts(runtime, kSpyro3);
  checkPlan(runtime, kSpyro3);
}

// The negative half: a window that is not there admits nothing, and a window that is present but
// does not reach the leaf is reported as such. Without this, a check that passed for the wrong
// reason -- an all-zero plan with no leaves at all -- would look green.
void test_a_window_that_does_not_reach_a_leaf_admits_nothing() {
  PlatformHlePlan plan{};
  plan.windowLo[0] = 0x80000000u;
  plan.windowHi[0] = kSpyro3.vsync; // half-open: the leaf itself is NOT inside
  CHECK(!windowAdmits(plan, kSpyro3.vsync));
  CHECK(!windowAdmits(plan, 0u)); // a zero high disables the slot entirely
  plan.windowHi[0] = kSpyro3.vsync + 4u;
  CHECK(windowAdmits(plan, kSpyro3.vsync));
}

} // namespace

int main() {
  RUN(spyro2_facts_and_plan_match_its_executable);
  RUN(spyro3_facts_and_plan_match_its_executable);
  RUN(a_window_that_does_not_reach_a_leaf_admits_nothing);
  return pt_summary();
}
