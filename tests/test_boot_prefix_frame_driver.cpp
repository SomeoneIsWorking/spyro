// The shared boot-prefix frame driver: one finite guest call, one delivered field per guest display
// wait, and exactly one presented field per product step. Both Spyro 2 and Spyro 3 compose this
// owner with their own `BootPrefixFacts`, so this file drives it over SYNTHETIC facts and leaves
// each title's measured addresses to `test_boot_prefix_facts.cpp`.
//
// The guest is real MIPS reached through real JIT execution, and that is forced by the framework as
// well as being the point. A NATIVE body that asks for a frame-boundary exit is refused --
// `invokeNativeFunction` requires a completed call and aborts with "required a completed guest
// call, but execution exited as frame-boundary" -- because only a leaf the guest REACHED BY `jal`
// gets a continuation to resume at. Each synthetic body below is therefore instructions: a `jal` to
// a counting leaf, one `jal` to the libetc VSync address per display wait, then `jr $ra`.
//
// The positive case pins the claims the boot rests on. The negatives are the ones a green run
// cannot distinguish from a working one:
//
//   * a guest call entered with `$r[31]` inside guest RAM is REFUSED at begin(). The executor ends
//   a
//     call whose PC equals its return address, so such a call stops in the middle of itself and
//     reports a return the boot prefix never made.
//   * a negative VSync argument is answered from the framework's DERIVED root counter 1 rather than
//   a
//     word the host owns, and a write to it is discarded -- both asserted, because the alternative
//     is documenting a counter nobody advances.
//   * a field delivery that arrives while one is in flight is REFUSED, so a field cannot be
//     double-delivered through a re-entrant path.
//   * a boot prefix that answers every display wait with another display wait is bounded by field
//     count and the run ends by name, on the same code path a wedged boot takes.
//   * a boot prefix that POLLS and never asks for a field is bounded by step count. They are
//     different failures and the second one is structurally invisible to the first.
//   * a limit the facts declare is the bound the driver enforces, and a title-supplied limit
//     replaces the shared default rather than being ignored.
//
// `Spyro2Runtime` is on the Game only because a Game needs some `GameRuntime`; the driver reads
// nothing title-specific from it.

#include "boot_prefix_frame_driver.h"
#include "config_vars.h"
#include "core.h"
#include "execution_control.h"
#include "execution_exit.h"
#include "execution_services.h"
#include "field_owner.h"
#include "game.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "runtime_run.h"
#include "spyro2_runtime.h"
#include "spyro_context.h"
#include "spyro_guest_call.h"
#include "testutil.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

// The addresses the driver enters. They are synthetic: each lies in the one image the fixture
// activates, which is all the driver needs of them.
constexpr std::uint32_t kBootPrefix = 0x80021000u;
constexpr std::uint32_t kFrameUpdate = 0x80021100u;
constexpr std::uint32_t kFrameDraw = 0x80021200u;
constexpr std::uint32_t kVSync = 0x80021300u;

// What a title's libetc VSync reports for a negative argument: the framework's ROOT COUNTER 1, an
// HBlank-clocked timer derived from delivered display time, with no writable state.
constexpr std::uint32_t kVBlankCounter = 0x1F801110u;

constexpr spyro::BootPrefixFacts kFacts{
    .titleName = "Synthetic title",
    .callName = "synthetic-boot-prefix-step",
    .bootPrefix = kBootPrefix,
    .frameUpdate = kFrameUpdate,
    .frameDraw = kFrameDraw,
    .field =
        {
            .titleName = "Synthetic title",
            .handlerStackTop = 0x8000E000u,
            .handlerStackBytes = 8192u,
            .fieldsPerLogicFrame = 2,
        },
};

// The facts with the two boot bounds replaced, which is how a title states a limit of its own.
constexpr spyro::BootPrefixFacts withLimits(std::uint64_t fieldLimit, std::uint64_t stepLimit) {
  spyro::BootPrefixFacts facts = kFacts;
  facts.bootStepFieldLimit = fieldLimit;
  facts.bootStepLimit = stepLimit;
  return facts;
}

constexpr std::uint32_t kBodies = 0x80020000u;
constexpr std::uint32_t kBootBody = kBodies + 0x000u;
constexpr std::uint32_t kUpdateBody = kBodies + 0x100u;
constexpr std::uint32_t kDrawBody = kBodies + 0x200u;
constexpr std::uint32_t kEndlessBody = kBodies + 0x300u;
// A body that never returns and never asks for a field: `jr $zero` is the PSX encoding of an
// unconditional jump to the address in `$zero`, which is 0, so the guest spins on its own budget.
std::vector<std::uint32_t> spinningBody() {
  return {0x10000000u};
}

// A separate ENTRY for the never-returning body. It cannot share one with its body: install()
// writes a `j <body>` at the entry, and when the two are the same address that `j` overwrites the
// body's first instruction with a jump to itself -- an endless loop that entered no leaf at all and
// read as a driver that never stalls.
constexpr std::uint32_t kEndlessEntry = kBodies + 0x500u;
constexpr std::uint32_t kSpinningBody = kBodies + 0x600u;
constexpr std::uint32_t kSpinningEntry = kBodies + 0x700u;

// One counting leaf per body, so "the update ran twice" is a named number rather than a field count
// two different bodies could have produced between them.
constexpr std::uint32_t kLeaves = 0x80030000u;
constexpr std::uint32_t kCountBoot = kLeaves + 0x00u;
constexpr std::uint32_t kCountUpdate = kLeaves + 0x10u;
constexpr std::uint32_t kCountDraw = kLeaves + 0x20u;
constexpr std::uint32_t kCountEndless = kLeaves + 0x30u;

std::uint32_t gVisitBoot = 0;
std::uint32_t gVisitUpdate = 0;
std::uint32_t gVisitDraw = 0;
std::uint32_t gVisitEndless = 0;
std::uint32_t gVBlankQueries = 0;
std::uint32_t gVBlankWaits = 0;

constexpr std::uint32_t kNop = 0x00000000u;
constexpr std::uint32_t kJrRa = 0x03E00008u;
constexpr std::uint32_t kJumpOpcodes = 0x08000000u; // `j`
constexpr std::uint32_t kCallOpcodes = 0x0C000000u; // `jal`

// KSEG0 masks with 0x1FFFFFFF, so 0x80010000 is physical 0x00010000. One image covers the entered
// addresses and both leaf families, because a native override key is (image identity, address) and
// a key built against an identity that does not cover its address silently never fires.
constexpr std::uint32_t kImageLo = 0x00010000u;
constexpr std::uint32_t kImageHi = 0x00060000u;

std::uint32_t jump(std::uint32_t target, std::uint32_t opcodes) {
  return opcodes | ((target >> 2u) & 0x03FFFFFFu);
}

std::uint32_t addiu(std::uint32_t rs, std::uint32_t rt, std::int32_t imm) {
  return (0x09u << 26) | (rs << 21u) | (rt << 16u) | (static_cast<std::uint32_t>(imm) & 0xFFFFu);
}

std::uint32_t sw(std::uint32_t rt, std::uint32_t base, std::int32_t off) {
  return (0x2Bu << 26) | (base << 21u) | (rt << 16u) | (static_cast<std::uint32_t>(off) & 0xFFFFu);
}

std::uint32_t lw(std::uint32_t rt, std::uint32_t base, std::int32_t off) {
  return (0x23u << 26) | (base << 21u) | (rt << 16u) | (static_cast<std::uint32_t>(off) & 0xFFFFu);
}

// A body that counts its own visit, takes `waits` display waits, and returns.
//
// The frame is not decoration. A `jal` leaves its OWN address+8 in `$r[31]`, so a body with a bare
// `jr ra` and no saved frame returns to itself and spins there forever -- which the first version
// of this body did, and it looked exactly like a driver that never retires its call. The shape here
// is the retail one (`addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)` ... `lw $ra,0x14($sp)`), because that
// is also what proves `GuestCall`'s captured return address is the one the guest ends at.
std::vector<std::uint32_t> body(std::uint32_t counterLeaf, std::uint32_t waits) {
  std::vector<std::uint32_t> words{
      addiu(29u, 29u, -0x18), // addiu $sp,$sp,-0x18
      sw(31u, 29u, 0x14),     // sw    $ra,0x14($sp)
      jump(counterLeaf, kCallOpcodes),
      kNop, // the `jal` delay slot
  };
  for (std::uint32_t wait = 0; wait < waits; ++wait) {
    words.push_back(jump(kVSync, kCallOpcodes));
    words.push_back(kNop); // the `jal` delay slot
  }
  words.push_back(lw(31u, 29u, 0x14));    // lw    $ra,0x14($sp)
  words.push_back(addiu(29u, 29u, 0x18)); // addiu $sp,$sp,0x18
  words.push_back(kJrRa);
  words.push_back(kNop);
  return words;
}

struct BodySpec {
  std::uint32_t entry;
  std::uint32_t target;
  std::uint32_t counterLeaf;
  const char *counterName;
  std::uint32_t waits;
};

constexpr BodySpec kBodiesSpec[] = {
    {kBootPrefix, kBootBody, kCountBoot, "synthetic-boot-prefix-count-boot", 1},
    {kFrameUpdate, kUpdateBody, kCountUpdate, "synthetic-boot-prefix-count-update", 0},
    {kFrameDraw, kDrawBody, kCountDraw, "synthetic-boot-prefix-count-draw", 1},
    // A boot prefix that never returns: far more waits than the bound the test sets.
    {kEndlessEntry, kEndlessBody, kCountEndless, "synthetic-boot-prefix-count-endless", 40},
    {kSpinningEntry, kSpinningBody, 0u, "synthetic-boot-prefix-spin", 0u},
};

void countLeaf(Core *core) {
  if (core->pc == kCountBoot) {
    ++gVisitBoot;
  } else if (core->pc == kCountUpdate) {
    ++gVisitUpdate;
  } else if (core->pc == kCountDraw) {
    ++gVisitDraw;
  } else {
    ++gVisitEndless;
  }
}

// The libetc leaf, as the framework's own boundary: a negative argument is a QUERY and answers the
// counter, anything else is a wait and leaves through the typed frame-boundary exit. The library
// body at 0x80058EDC never runs in the product, so it does not run here either.
void vsyncLeaf(Core *core) {
  if (static_cast<std::int32_t>(core->r[4]) < 0) {
    ++gVBlankQueries;
    core->r[2] = core->mem_r32(kVBlankCounter);
    return;
  }
  ++gVBlankWaits;
  psx::cpu::requestExecutionExit(*core, psx::cpu::ExecutionExitReason::FrameBoundary);
}

// The Game, with the two things a title runtime needs on it already in place. This is a FUNCTION
// rather than fixture body code on purpose: the frame driver's own constructor reaches
// `spyro_context(core)` to publish its field owner, and members are initialized before the
// fixture's body runs, so a context published in the body is too late and the driver aborts.
std::unique_ptr<Game> newGame(SpyroContext &context, spyro2::Spyro2Runtime &runtime) {
  std::unique_ptr<Game> game = std::make_unique<Game>();
  game->core.gameCtx = &context;
  game->runtime = &runtime;
  game->gpu_dev.s_gpu_on = 0; // exercise the real Core presentation backend without a window
  // The synthetic bodies push a real frame, so they need a real stack. A Core starts with `$sp` at
  // 0, and `addiu $sp,$sp,-0x18` then gives 0xFFFFFFE8 -- which Lightrec correctly rejects as an
  // invalid access. The product's own entry gets this from the executable header (`loadPsxExeImage`
  // writes `image.stackPointer` into `$r[29]`/`$r[30]`), so the fixture sets the same pair.
  game->core.r[29] = 0x801FFF00u;
  game->core.r[30] = 0x801FFF00u;
  return game;
}

class BootFixture {
public:
  explicit BootFixture(const spyro::BootPrefixFacts &facts = kFacts)
      : game(newGame(context, runtime)), driver(*game, facts) {}

  bool install() {
    Core &core = game->core;
    // Each entered address is a `j` to its body, so the call is entered through shipping JIT
    // execution and the framework's leaf dispatch rather than called from C++.
    for (const BodySpec &spec : kBodiesSpec) {
      const std::vector<std::uint32_t> words =
          spec.counterLeaf == 0u ? spinningBody() : body(spec.counterLeaf, spec.waits);
      for (std::size_t index = 0; index < words.size(); ++index) {
        core.mem_w32(spec.target + static_cast<std::uint32_t>(index) * 4u, words[index]);
      }
      core.mem_w32(spec.entry, jump(spec.target, kJumpOpcodes));
      core.mem_w32(spec.entry + 4u, kNop);
    }
    // The image is activated AFTER the code words are written, and that order is the contract, not
    // a convenience. A guest write into resident code runs the framework's central executable
    // invalidation, which removes the written bytes from the image's residency; activate first and
    // the `j` above deletes the very range the dispatch is about to resolve, and the run fails with
    // "in no loaded code image" while the catalog still reports one active image. The product's
    // order is the same one: `loadPsxExeImage` copies the text, publishes the write, then
    // activates.
    const auto image =
        core.imageCatalog().activate("synthetic-boot-prefix", {kImageLo, kImageHi}, 0x53503220u);
    const auto resolved = core.imageCatalog().resolve(kBootPrefix);
    if (!resolved.has_value() || *resolved != image) {
      std::fprintf(
          stderr,
          "FIXTURE REFUSED: [%u,%u) does not own the boot prefix 0x%08X (physical 0x%08X); "
          "active images: %zu\n",
          kImageLo,
          kImageHi,
          kBootPrefix,
          kBootPrefix & 0x1FFFFFFFu,
          core.imageCatalog().activeCount());
      std::abort();
    }
    if (!core.nativeDispatcher().install(
            {{image, kVSync}, "synthetic-boot-prefix-vsync", vsyncLeaf})) {
      return false;
    }
    for (const BodySpec &spec : kBodiesSpec) {
      if (!core.nativeDispatcher().install(
              {{image, spec.counterLeaf}, spec.counterName, countLeaf})) {
        return false;
      }
    }
    return true;
  }

  // Point the boot prefix at the body that never returns, for the bounded-boot case.
  // A boot prefix that spins without asking for a field. See the case that uses it.
  void useSpinningBoot() {
    game->core.mem_w32(kBootPrefix, jump(kSpinningEntry, kJumpOpcodes));
  }

  void useEndlessBoot() {
    Core &core = game->core;
    core.mem_w32(kBootPrefix, jump(kEndlessEntry, kJumpOpcodes));
    core.mem_w32(kBootPrefix + 4u, kNop);
  }

  spyro2::Spyro2Runtime runtime;
  SpyroContext context;
  std::unique_ptr<Game> game;
  spyro::BootPrefixFrameDriver driver;
};

// The per-turn budget in a fixture Core is small, so a body takes several turns to retire. The
// driver resumes a call across product steps, so these cases count STEPS and turns rather than
// assuming one of each -- the contract under test is one presented field per step and one delivered
// field per guest wait, both of which are totals.
constexpr std::uint32_t kStepCap = 64;

void stepUntilBootComplete(spyro::BootPrefixFrameDriver &driver, Core &core) {
  for (std::uint32_t step = 0; step < kStepCap && !driver.bootComplete(); ++step) {
    driver.stepFrame(core, step);
  }
}

bool advanceUntilReturned(spyro::GuestCall &call, std::uint32_t turnCap) {
  for (std::uint32_t turn = 0; turn < turnCap; ++turn) {
    if (call.advance(1).outcome == spyro::GuestCall::Outcome::Returned) {
      return true;
    }
  }
  return false;
}

void resetCounters() {
  gVisitBoot = 0;
  gVisitUpdate = 0;
  gVisitDraw = 0;
  gVisitEndless = 0;
  gVBlankQueries = 0;
  gVBlankWaits = 0;
}

void test_boot_presents_one_field_per_guest_wait() {
  psx::config::cv_nopace.set(psx::config::Layer::Runtime, true);
  psx::config::cv_repl.set(psx::config::Layer::Runtime, false);
  resetCounters();
  BootFixture fixture;
  CHECK(fixture.install());
  Core &core = fixture.game->core;
  fixture.driver.initialize();
  CHECK(!fixture.driver.bootComplete());

  const std::uint64_t stepsTaken = [&] {
    std::uint64_t steps = 0;
    while (!fixture.driver.bootComplete() && steps < kStepCap) {
      fixture.driver.stepFrame(core, static_cast<std::uint32_t>(steps));
      ++steps;
    }
    return steps;
  }();
  // The boot body is entered once and waits once; every step presents exactly one field of its own,
  // so fields == waits + steps with nothing else counted. A driver that delivered a field for
  // something beyond the guest's own waits, or presented twice in a step, moves one of these.
  CHECK(fixture.driver.bootComplete());
  CHECK(stepsTaken > 0u);
  CHECK_EQ(gVisitBoot, 1u);
  CHECK_EQ(gVBlankWaits, 1u);
  CHECK_EQ(fixture.driver.fields().fields(), 1u + stepsTaken);
  CHECK_EQ(fixture.driver.fields().presents(), stepsTaken);
  // The counter the guest's VSync reports is derived from delivered display time, so it must have
  // MOVED with the fields. Asserting it EQUALS the field count is the assertion that caught an
  // earlier false claim in the product: the register is HBlank-clocked, so it is a scanline count.
  CHECK(core.mem_r32(kVBlankCounter) > 0u);
  CHECK(core.lightrecExecutor().counters().executedBlocks > 0u);

  // The main loop is the retail pair: the update, which waits for nothing, and the draw, which
  // owns the frame's display wait. The update count is bounded by the draw count because the draw
  // only follows a returned update, and the draw count cannot exceed the steps taken.
  for (std::uint32_t step = 0; step < 8; ++step) {
    fixture.driver.stepFrame(core, step);
  }
  CHECK(gVisitUpdate > 0u);
  CHECK(gVisitDraw > 0u);
  CHECK(gVisitUpdate <= gVisitDraw);    // no draw without its update
  CHECK(gVisitDraw <= 8u + stepsTaken); // at most one draw pair per step
  CHECK_EQ(fixture.driver.fields().presents(), stepsTaken + 8u);
}

void test_guest_call_refuses_a_return_address_inside_guest_ram() {
  resetCounters();
  BootFixture fixture;
  CHECK(fixture.install());
  Core &core = fixture.game->core;
  spyro::GuestCall call(core, "synthetic-boot-prefix-call");

  core.r[31] = kBootPrefix; // a guest address: an "early return" that is not a return
  call.begin(kBootPrefix);
  CHECK(!call.active());
  CHECK_EQ(call.entry(), 0u);
  const auto refused = call.advance(4);
  CHECK(refused.outcome == spyro::GuestCall::Outcome::Returned);
  CHECK_EQ(refused.guestPc, 0u);
  CHECK_EQ(gVisitBoot, 0u); // nothing ran

  // The same call with a host return address is finite, and it does run. One turn is not enough for
  // a body with two display waits, so this asserts the call stays active ACROSS a boundary rather
  // than claiming a return it has not made.
  core.r[31] = 0x00000001u;
  call.begin(kBootPrefix);
  CHECK(call.active());
  const auto first = call.advance(1);
  CHECK(first.outcome == spyro::GuestCall::Outcome::FieldBoundary);
  CHECK(call.active());
  CHECK(call.resumePc() != 0u);
  // The call stays active across the display wait and returns once the body retires, which is the
  // whole point of capturing the return address before the first dispatch: the body overwrites
  // `$r[31]` on its way to its own `jal`.
  CHECK(advanceUntilReturned(call, 64u));
  CHECK_EQ(gVisitBoot, 1u);
  CHECK_EQ(gVBlankWaits, 1u);
  CHECK(!call.active());
}

void test_boot_prefix_that_never_returns_is_bounded() {
  resetCounters();
  // A low bound, so the refusal is observed on the SAME code path a wedged boot takes. The body
  // waits 40 times and the shared default is far above that, so lowering it means the case does not
  // have to deliver the default's worth of fields to prove the bound exists -- and that the limit a
  // title's facts declare is the one enforced.
  BootFixture fixture(withLimits(4u, spyro::kDefaultBootStepLimit));
  CHECK(fixture.install());
  fixture.useEndlessBoot();
  Core &core = fixture.game->core;
  fixture.driver.initialize();

  // The bound is enforced where the waits are delivered, so it fires within the first step. The
  // step cap here is the test's own: a driver that never stalls would otherwise spin this loop
  // forever, which is the same failure it is looking for.
  for (std::uint32_t step = 0; step < 16u && !fixture.driver.bootStalled(); ++step) {
    fixture.driver.stepFrame(core, step);
  }
  CHECK(fixture.driver.bootStalled());
  CHECK(!fixture.driver.bootComplete());
  CHECK(gVisitEndless > 0u);
  CHECK(gVBlankWaits >= 4u);
  // Every field the owner delivered is a guest wait except the step's own fence, so the totals are
  // waits + 1 and no more than the bound + that one.
  CHECK_EQ(fixture.driver.fields().fields(), gVBlankWaits + 1u);
  CHECK(fixture.driver.fields().fields() <= 4u + 1u);
  CHECK_EQ(fixture.driver.fields().presents(), 1u);
  // The run is asked to end by name rather than spun on, through the framework's own completion
  // path, so the process still reaches its shutdown accounting.
  CHECK(spyro::runtimeRun(core).shouldEnd());
}

// A boot prefix that never returns AND never asks for a field -- the second way the prefix can
// hang, and the one the field bound structurally cannot see, because it is enforced where a field
// is DELIVERED. Both titles' module loaders have this shape in the retail image: they poll a status
// word and reach no further display wait, so the run would keep stepping until the process's own
// frame cap and report a run length instead of a diagnosis.
void test_boot_prefix_that_polls_without_asking_for_a_field_is_bounded() {
  resetCounters();
  // The FIELD bound is deliberately left at its default: the body delivers none, so a driver that
  // only checked it would never stall.
  BootFixture fixture(withLimits(spyro::kDefaultBootStepFieldLimit, 3u));
  CHECK(fixture.install());
  fixture.useSpinningBoot();
  Core &core = fixture.game->core;
  fixture.driver.initialize();

  for (std::uint32_t step = 0; step < 16u && !fixture.driver.bootStalled(); ++step) {
    fixture.driver.stepFrame(core, step);
  }
  CHECK(fixture.driver.bootStalled());
  CHECK(!fixture.driver.bootComplete());
  // Nothing was ever asked for, so no guest wait happened: this is what makes it the polling case
  // rather than the display-wait case above.
  CHECK_EQ(gVBlankWaits, 0u);
  // Only the step fences the owner delivers itself: one per step, and the refusal step is still a
  // step. The framework's frame driver checks exactly one presented field per product step, so a
  // step that refused the guest and skipped its fence would abort the process on the very step that
  // named the stall.
  CHECK_EQ(fixture.driver.fields().presents(), 4u);
  CHECK(spyro::runtimeRun(core).shouldEnd());
}

// A re-entrant delivery must be REFUSED, not served: a nested field would be a second field for one
// physical one, and the owner is the only place that can be counted. The observer is where the
// shared owner calls back into title code inside a delivery, so it is where a re-entrant request
// can arrive.
class ReentrantObserver final : public spyro::FieldObserver {
public:
  // The owner is handed over after construction, because the owner takes the observer. The observer
  // exists to call back INTO the owner from inside its own delivery, so a test that cannot wire the
  // two is testing nothing.
  void attach(spyro::FieldOwner &owner) {
    owner_ = &owner;
  }

  void onField(Core &core, bool startEdge) override {
    (void)startEdge;
    ++seen_;
    if (owner_ != nullptr && nestedMade_ == nullptr) {
      bool accepted = owner_->deliver({.site = "reentrant", .present = false, .pace = false});
      nestedMade_ = &accepted;
      nested_ = accepted;
    }
  }

  [[nodiscard]] bool nestedAccepted() const {
    return nested_;
  }

  [[nodiscard]] bool nestedMade() const {
    return nestedMade_ != nullptr;
  }

  [[nodiscard]] std::uint32_t seen() const {
    return seen_;
  }

private:
  spyro::FieldOwner *owner_ = nullptr;
  std::uint32_t seen_ = 0;
  // Two cells, because the result of the nested call is a temporary whose address cannot be taken.
  // `nested_` is the value and `nestedMade_` says the call was made at all, so a case that never
  // reached it reports "no nested delivery" instead of a refusal.
  bool nested_ = false;
  bool *nestedMade_ = nullptr;
};

void test_reentrant_field_delivery_is_refused() {
  psx::config::cv_nopace.set(psx::config::Layer::Runtime, true);
  psx::config::cv_repl.set(psx::config::Layer::Runtime, false);
  SpyroContext context;
  spyro2::Spyro2Runtime runtime;
  std::unique_ptr<Game> game = newGame(context, runtime);

  ReentrantObserver observer;
  spyro::FieldOwner owner(*game,
                          spyro::FieldOwnerFacts{
                              .titleName = "Synthetic title (observer case)",
                              .handlerStackTop = 0x8000E000u,
                              .handlerStackBytes = 8192u,
                              .fieldsPerLogicFrame = 1,
                          },
                          &observer);
  observer.attach(owner);

  CHECK(owner.deliver({.site = "outer", .present = false, .pace = false}));
  CHECK_EQ(observer.seen(), 1u);
  CHECK(observer.nestedMade());
  CHECK(!observer.nestedAccepted());
  // One delivery, one field, no second one from the refused request.
  CHECK_EQ(owner.fields(), 1u);
  CHECK_EQ(owner.presents(), 0u);
}

void test_negative_vsync_query_answers_the_derived_counter() {
  // A Core with its Game, not a bare one: the counter word is served by the framework's peripheral
  // model, which dereferences `core.game`. A bare Core answers the query with a null dereference
  // instead of a value, which reads as a crash in the leaf rather than as the missing owner.
  SpyroContext context;
  spyro2::Spyro2Runtime runtime;
  std::unique_ptr<Game> game = newGame(context, runtime);
  Core &core = game->core;
  // Before any display time, the counter answers 0 rather than a plausible field number: the query
  // is a measurement, and a caller reading a stale nonzero value here has no way to tell it apart.
  core.r[4] = -1;
  vsyncLeaf(&core);
  CHECK_EQ(core.r[2], 0u);
  CHECK_EQ(gVBlankQueries, 1u);

  // A WRITE to the counter is discarded -- the framework's root counter 1 is derived and has no
  // writable state -- so a value stored here does not survive. That is precisely why the field
  // owner owns no word for this counter, and the case pins the discard instead of assuming it.
  core.mem_w32(kVBlankCounter, 7u);
  const std::uint32_t derived = core.mem_r32(kVBlankCounter);
  CHECK(derived != 7u);
  core.r[4] = -1;
  vsyncLeaf(&core);
  CHECK_EQ(core.r[2], derived);

  // And a nonnegative argument is a wait, never a read: the library body is not executed and the
  // call leaves through the typed boundary.
  core.r[4] = 0;
  vsyncLeaf(&core);
  CHECK_EQ(core.r[2], derived); // unchanged by a wait
  const auto pending = core.executionControl().consume();
  CHECK(pending.has_value());
  CHECK(pending->reason == psx::cpu::ExecutionExitReason::FrameBoundary);
  CHECK(!core.executionControl().consume().has_value()); // consumed exactly once
}

} // namespace

int main() {
  RUN(boot_presents_one_field_per_guest_wait);
  RUN(guest_call_refuses_a_return_address_inside_guest_ram);
  RUN(boot_prefix_that_never_returns_is_bounded);
  RUN(boot_prefix_that_polls_without_asking_for_a_field_is_bounded);
  RUN(reentrant_field_delivery_is_refused);
  RUN(negative_vsync_query_answers_the_derived_counter);
  return pt_summary();
}
