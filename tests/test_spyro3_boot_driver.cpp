// Spyro 3's boot driver: one finite guest call, one delivered field per guest display wait, and
// exactly one presented field per product step.
//
// The guest is real MIPS reached through real JIT execution, and that is forced by the framework as
// well as being the point. A NATIVE body that asks for a frame-boundary exit is refused --
// `invokeNativeFunction` requires a completed call and aborts with "required a completed guest
// call, but execution exited as frame-boundary" -- because only a leaf the guest REACHED BY `jal`
// gets a continuation to resume at. Each synthetic body below is therefore instructions: a `jal` to
// a counting leaf, one `jal 0x8005956C` per display wait, then `jr $ra`.
//
// This file is the twin of `test_spyro2_boot_driver.cpp` and shares its contract, because the two
// titles run through the SAME title-neutral owners (`game/core/field_owner.*` and
// `game/core/spyro_guest_call.*`) with different constants. That is deliberate: a second copy of
// the resume and delivery contract would be a second thing to keep in step, and the per-title facts
// -- the boot prefix, the two frame leaves, the VSync address and the counter it reports -- are the
// only things that differ, and each is quoted from SCUS_944.67's own bytes below.
//
// The positive case pins the three claims the boot rests on. The negatives are the ones a green run
// cannot distinguish from a working one:
//
//   * a guest call entered with `$r[31]` inside guest RAM is REFUSED at begin(). The executor ends
//     a call whose PC equals its return address, so such a call stops in the middle of itself and
//     reports a return the boot prefix never made.
//   * a negative VSync argument is answered from the counter this title's executable names, that
//     counter is the framework's DERIVED root counter 1 rather than a word the host owns, and a
//     write to it is discarded.
//   * a field delivery that arrives while one is in flight is REFUSED.
//   * a boot prefix that answers every display wait with another display wait is bounded by field
//     count, and one that POLLS and never asks for a field is bounded by step count. They are
//     different failures and the second one is structurally invisible to the first.
//   * every address the runtime's measured plan declares lies inside one of the plan's own
//     admission windows. A leaf outside them is REFUSED at registration with "no direct-runtime
//     hardware-service window admits 0x...", which in a run with a disc attached reads as a boot
//     that stops for an unrelated reason.
//
// The addresses are the same constants spyro3_frame_driver.cpp and spyro3_runtime.cpp name, so a
// driver or a plan that entered a different one fails here rather than only in a run with a disc.

#include "config_vars.h"
#include "core.h"
#include "execution_control.h"
#include "execution_exit.h"
#include "execution_services.h"
#include "field_owner.h"
#include "game.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "platform_hle.h"
#include "runtime_run.h"
#include "spyro3_frame_driver.h"
#include "spyro3_runtime.h"
#include "spyro_context.h"
#include "spyro_guest_call.h"
#include "testutil.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

// The addresses the driver enters, matching spyro3_frame_driver.cpp.
constexpr std::uint32_t kBootPrefix = 0x8002AB38u;
constexpr std::uint32_t kFrameUpdate = 0x80055400u;
constexpr std::uint32_t kFrameDraw = 0x8001E638u;
constexpr std::uint32_t kVSync = 0x8005956Cu;

// The counter the title's libetc VSync reports for a negative argument. It is NOT chosen here: the
// executable's own initialised data at 0x80069F2C holds 0x1F801110 (the mode-1 counter pointer
// VSync polls at 0x80059578), and 0x80069F28 holds 0x1F801814. It is the framework's ROOT COUNTER
// 1, an HBlank-clocked timer derived from delivered display time, with no writable state.
constexpr std::uint32_t kVBlankCounter = 0x1F801110u;

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
// `jr ra` and no saved frame returns to itself and spins there forever. The shape here is the
// retail one (`addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)` ... `lw $ra,0x14($sp)`), which is also what
// proves `GuestCall`'s captured return address is the one the guest ends at.
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
    {kBootPrefix, kBootBody, kCountBoot, "synthetic-spyro3-count-boot", 1},
    {kFrameUpdate, kUpdateBody, kCountUpdate, "synthetic-spyro3-count-update", 0},
    {kFrameDraw, kDrawBody, kCountDraw, "synthetic-spyro3-count-draw", 1},
    // A boot prefix that never returns: far more waits than the bound the test sets.
    {kEndlessEntry, kEndlessBody, kCountEndless, "synthetic-spyro3-count-endless", 40},
    {kSpinningEntry, kSpinningBody, 0u, "synthetic-spyro3-spin", 0u},
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
// body at 0x8005956C never runs in the product, so it does not run here either.
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
std::unique_ptr<Game> newGame(SpyroContext &context, spyro3::Spyro3Runtime &runtime) {
  std::unique_ptr<Game> game = std::make_unique<Game>();
  game->core.gameCtx = &context;
  game->runtime = &runtime;
  game->gpu_dev.s_gpu_on = 0; // exercise the real Core presentation backend without a window
  // The synthetic bodies push a real frame, so they need a real stack. A Core starts with `$sp` at
  // 0, and `addiu $sp,$sp,-0x18` then gives 0xFFFFFFE8 -- which Lightrec correctly rejects as an
  // invalid access. The product's own entry gets this from the executable header.
  game->core.r[29] = 0x801FFF00u;
  game->core.r[30] = 0x801FFF00u;
  return game;
}

class BootFixture {
public:
  BootFixture() : game(newGame(context, runtime)), driver(*game) {}

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
    // a convenience: a guest write into resident code runs the framework's central executable
    // invalidation, which removes the written bytes from the image's residency.
    const auto image =
        core.imageCatalog().activate("synthetic-spyro3", {kImageLo, kImageHi}, 0x53503320u);
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
    if (!core.nativeDispatcher().install({{image, kVSync}, "synthetic-spyro3-vsync", vsyncLeaf})) {
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

  // A boot prefix that answers every display wait with another display wait.
  void useEndlessBoot() {
    Core &core = game->core;
    core.mem_w32(kBootPrefix, jump(kEndlessEntry, kJumpOpcodes));
    core.mem_w32(kBootPrefix + 4u, kNop);
  }

  // A boot prefix that spins without asking for a field. See the case that uses it.
  void useSpinningBoot() {
    game->core.mem_w32(kBootPrefix, jump(kSpinningEntry, kJumpOpcodes));
  }

  spyro3::Spyro3Runtime runtime;
  SpyroContext context;
  std::unique_ptr<Game> game;
  spyro3::Spyro3FrameDriver driver;
};

// The per-turn budget in a fixture Core is small, so a body takes several turns to retire. The
// driver resumes a call across product steps, so these cases count STEPS and turns rather than
// assuming one of each -- the contract under test is one presented field per step and one delivered
// field per guest wait, both of which are totals.
constexpr std::uint32_t kStepCap = 64;

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
  fixture.driver.initialize(core);
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
  // earlier false claim in this lineage: the register is HBlank-clocked, so it is a scanline count.
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
  spyro::GuestCall call(core, "synthetic-spyro3-call");

  core.r[31] = kBootPrefix; // a guest address: an "early return" that is not a return
  call.begin(kBootPrefix);
  CHECK(!call.active());
  CHECK_EQ(call.entry(), 0u);
  const auto refused = call.advance(4);
  CHECK(refused.outcome == spyro::GuestCall::Outcome::Returned);
  CHECK_EQ(refused.guestPc, 0u);
  CHECK_EQ(gVisitBoot, 0u); // nothing ran

  // The same call with a host return address is finite, and it does run. One turn is not enough for
  // a body with one display wait, so this asserts the call stays active ACROSS a boundary rather
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
  BootFixture fixture;
  CHECK(fixture.install());
  fixture.useEndlessBoot();
  Core &core = fixture.game->core;
  // A low bound, so the refusal is observed on the SAME code path a wedged boot takes. The default
  // is 64 fields and the body waits 40 times, so lowering it means the case does not have to
  // deliver 64 fields to prove the bound exists.
  fixture.driver.setBootStepFieldBound(4u);
  fixture.driver.initialize(core);

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
// is DELIVERED. Spyro 3's module loader polls a status word and reaches no further display wait on
// the path this port can serve, so the run would keep stepping until the process's own frame cap
// and report a run length instead of a diagnosis.
void test_boot_prefix_that_polls_without_asking_for_a_field_is_bounded() {
  resetCounters();
  BootFixture fixture;
  CHECK(fixture.install());
  fixture.useSpinningBoot();
  Core &core = fixture.game->core;
  // The FIELD bound is deliberately left at its default and lowered far below anything this body
  // could deliver: it delivers none, so a driver that only checked it would never stall.
  fixture.driver.setBootStepBound(3u);
  fixture.driver.initialize(core);

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
    (void)core;
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
  spyro3::Spyro3Runtime runtime;
  std::unique_ptr<Game> game = newGame(context, runtime);

  ReentrantObserver observer;
  spyro::FieldOwner owner(*game,
                          spyro::FieldOwnerFacts{
                              .titleName = "Spyro 3 (observer case)",
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
  spyro3::Spyro3Runtime runtime;
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

// The measured plan is the one place a wrong guest address becomes invisible until a run with a
// disc attached: every leaf is refused at registration with "no direct-runtime hardware-service
// window admits 0x...", which reads there as a boot that stopped for an unrelated reason. Each
// address the plan declares must therefore lie inside one of the plan's OWN windows, and the plan
// must actually declare one -- a plan with a VSync address of 0 is refused earlier still, by
// `requireNativeFrameLoopContract`, with a message about a retail busy-wait.
void test_every_declared_leaf_lies_inside_a_declared_window() {
  const spyro3::Spyro3Runtime runtime;
  const PlatformHlePlan *plan = runtime.platformHlePlan();
  CHECK(plan != nullptr);
  if (plan == nullptr) {
    return;
  }
  CHECK_EQ(plan->vsyncAddress, kVSync);
  CHECK(plan->vsyncAddress != 0u);
  CHECK_EQ(plan->vsyncQueryCounterAddress, kVBlankCounter);

  struct Declared {
    const char *name;
    std::uint32_t address;
  };
  // A zero address means "not located", and the framework installs nothing for it. Listing only the
  // ones this title claims is the point: the check is over the CLAIM, not over the array.
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
    bool admitted = false;
    for (int slot = 0; slot < kPlatformHleWindowCapacity; ++slot) {
      if (plan->windowHi[slot] != 0u && leaf.address >= plan->windowLo[slot] &&
          leaf.address < plan->windowHi[slot]) {
        admitted = true;
        break;
      }
    }
    if (!admitted) {
      std::fprintf(stderr,
                   "REFUSED: measured leaf %s 0x%08X lies outside every window the plan declares\n",
                   leaf.name,
                   leaf.address);
    }
    CHECK(admitted);
  }
  for (int index = 0; index < plan->bindingCount; ++index) {
    const std::uint32_t address = plan->bindings[index].addr;
    bool admitted = false;
    for (int slot = 0; slot < kPlatformHleWindowCapacity; ++slot) {
      if (plan->windowHi[slot] != 0u && address >= plan->windowLo[slot] &&
          address < plan->windowHi[slot]) {
        admitted = true;
        break;
      }
    }
    if (!admitted) {
      std::fprintf(stderr,
                   "REFUSED: measured binding %d at 0x%08X lies outside every window\n",
                   index,
                   address);
    }
    CHECK(admitted);
    CHECK(plan->bindings[index].fn != nullptr);
  }
  CHECK(plan->bindingCount >= 1);
  CHECK(plan->bindingCount <= PlatformHlePlan::kMaxBindings);
}

// The negative half of the same claim: a window that is not there cannot admit anything, and a
// window that is present but does not reach the leaf must be reported as such. Without this, a case
// that passed for the wrong reason -- an all-zero plan with no leaves at all -- would look green.
void test_a_window_that_does_not_reach_a_leaf_admits_nothing() {
  PlatformHlePlan plan{};
  plan.vsyncAddress = kVSync;
  plan.windowLo[0] = 0x80000000u;
  plan.windowHi[0] = kVSync; // half-open: the leaf itself is NOT inside
  const auto admitted = [&plan](std::uint32_t address) {
    for (int slot = 0; slot < kPlatformHleWindowCapacity; ++slot) {
      if (plan.windowHi[slot] != 0u && address >= plan.windowLo[slot] &&
          address < plan.windowHi[slot]) {
        return true;
      }
    }
    return false;
  };
  CHECK(!admitted(kVSync));
  CHECK(!admitted(0u)); // a zero high disables the slot entirely
  plan.windowHi[0] = kVSync + 4u;
  CHECK(admitted(kVSync));
}

} // namespace

int main() {
  RUN(boot_presents_one_field_per_guest_wait);
  RUN(guest_call_refuses_a_return_address_inside_guest_ram);
  RUN(boot_prefix_that_never_returns_is_bounded);
  RUN(boot_prefix_that_polls_without_asking_for_a_field_is_bounded);
  RUN(reentrant_field_delivery_is_refused);
  RUN(negative_vsync_query_answers_the_derived_counter);
  RUN(every_declared_leaf_lies_inside_a_declared_window);
  RUN(a_window_that_does_not_reach_a_leaf_admits_nothing);
  return pt_summary();
}
