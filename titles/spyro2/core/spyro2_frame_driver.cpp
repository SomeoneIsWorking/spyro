#include "spyro2_frame_driver.h"

#include "core.h"
#include "game.h"
#include "guest_call.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <lucent/log.h>

void interp_call(Core *core, std::uint32_t pc);

namespace spyro2 {
namespace {

constexpr std::uint32_t kGameMainFrameBytes = 24u;
constexpr std::uint32_t kBootPrefixFrameBytes = 24u;
constexpr std::uint32_t kStaticConstructors = 0x80054834u;
constexpr std::uint32_t kBootPrefixFirstLeaf = 0x800548A4u;
constexpr std::uint32_t kGameMainAfterConstructors = 0x80011AECu;
constexpr std::uint32_t kGameMainAfterBootPrefix = 0x80011AF4u;
constexpr std::uint32_t kBootPrefixAfterFirstLeaf = 0x80011EACu;
constexpr std::uint32_t kBootPrefixAfterDisplay = 0x80011EB4u;
constexpr std::uint32_t kBootPrefixAfterSpuBootstrap = 0x80011EBCu;
constexpr std::uint32_t kBootPrefixAfterCdBootstrap = 0x80011EC4u;
constexpr std::uint32_t kBootPrefixAfterCdMusicInit = 0x80011ECCu;
constexpr std::uint32_t kBootPrefixAfterGeometryInit = 0x80011ED4u;
constexpr std::uint32_t kBootPrefixAfterArchiveLoad = 0x80011F00u;
constexpr std::uint32_t kDisplayBootstrap = 0x80011BBCu;
constexpr std::uint32_t kSpuBootstrap = 0x80011B1Cu;
constexpr std::uint32_t kCdBootstrap = 0x80011B3Cu;
constexpr std::uint32_t kCdMusicInit = 0x80012B84u;
constexpr std::uint32_t kGeometryInit = 0x80011D24u;
constexpr std::uint32_t kArchiveLoad = 0x80013810u;
constexpr std::uint32_t kArchiveBaseLba = 0x800682D8u;
constexpr std::uint32_t kArchiveBuffer = 0x80011110u;
constexpr std::uint32_t kBootPayloadOffset = 0x800676E0u;
constexpr std::uint32_t kBootPayloadSize = 0x800676E4u;
constexpr std::uint32_t kLoadedPayloadSize = 0x800670C4u;
constexpr std::uint32_t kLoadedBootstrap = 0x80077374u;
constexpr std::uint32_t kAfterLoadedBootstrap = 0x80011F14u;

constexpr std::uint32_t kVSyncJal = 0x0C0163B7u;
constexpr std::array kLoadedVSyncCallsites{
    0x800772F4u,
    0x80077400u,
    0x800774E4u,
    0x80077504u,
    0x80077524u,
    0x80077534u,
    0x8007767Cu,
    0x8007769Cu,
    0x800778C4u,
    0x800778E4u,
    0x80077944u,
    0x80077954u,
    0x80077B74u,
    0x80077B98u,
};

void call(Core &core, std::uint32_t address, std::uint32_t returnAddress) {
  core.r[31] = returnAddress;
  rc0(&core, address);
}

} // namespace

Spyro2LoadedBootstrap::Spyro2LoadedBootstrap(Game &game) : game_(game) {}

void Spyro2LoadedBootstrap::runUntil(Core &core, std::uint32_t start, std::uint32_t stop) {
  core.r[31] = stop;
  interp_call(&core, start);
}

void Spyro2LoadedBootstrap::queryCounter(Core &core, std::uint32_t resume, std::uint32_t stop) {
  core.r[4] = 0xFFFFFFFFu;
  core.r[2] = static_cast<std::uint32_t>(game_.presentation.fence());
  runUntil(core, resume, stop);
}

void Spyro2LoadedBootstrap::deliverField(Core &core) {
  const std::uint32_t callsite = timing_.nextFieldCallsite();
  core.r[4] = callsite == 0x80077524u || callsite == 0x80077944u ? 4u : 0u;
  game_.presentation.commit(&core, 1);
  ++fieldsDelivered_;
  core.r[2] = static_cast<std::uint32_t>(game_.presentation.fence());
}

void Spyro2LoadedBootstrap::beginChild(Core &core, std::uint32_t returnAddress) {
  // Retained 0x800772A4 prologue. Saving the real parent return before interp_call installs its
  // private stop PC keeps the loaded body's guest stack byte-identical across all twelve fields.
  core.r[29] -= 48u;
  core.mem_w32(core.r[29] + 40u, core.r[20]);
  core.r[20] = core.r[4];
  core.mem_w32(core.r[29] + 28u, core.r[17]);
  core.r[17] = core.r[5];
  core.mem_w32(core.r[29] + 32u, core.r[18]);
  core.r[18] = core.r[6];
  core.mem_w32(core.r[29] + 24u, core.r[16]);
  core.r[16] = 4u;
  core.mem_w32(core.r[29] + 36u, core.r[19]);
  core.r[19] = core.r[7] - static_cast<std::uint32_t>(static_cast<std::int32_t>(core.r[18]) >> 11);
  core.mem_w32(core.r[29] + 44u, returnAddress);
  runUntil(core, 0x800772D8u, 0x800772F4u);
}

void Spyro2LoadedBootstrap::resumeChild(Core &core, std::uint32_t stop) {
  core.r[4] = 0u;
  runUntil(core, 0x800772FCu, stop);
}

void Spyro2LoadedBootstrap::beginClear(Core &core, std::uint32_t returnAddress) {
  // Retained 0x8004C484 prologue. Its DrawSync is synchronous; the only field is the nested
  // 0x8004C494 VSync(0), so stop immediately before that call.
  core.r[29] -= 32u;
  core.mem_w32(core.r[29] + 24u, returnAddress);
  runUntil(core, 0x8004C48Cu, 0x8004C494u);
}

void Spyro2LoadedBootstrap::resumeClear(Core &core, std::uint32_t stop) {
  core.r[4] = 0u;
  runUntil(core, 0x8004C49Cu, stop);
}

void Spyro2LoadedBootstrap::validateRetainedTimingMap(Core &core) const {
  const std::uint32_t payload = core.mem_r32(kArchiveBuffer);
  const std::uint32_t payloadSize = core.mem_r32(kLoadedPayloadSize);
  std::size_t found = 0u;
  for (std::uint32_t offset = 0u; offset + 4u <= payloadSize; offset += 4u) {
    if (core.mem_r32(payload + offset) != kVSyncJal) {
      continue;
    }
    const std::uint32_t callsite = payload + offset;
    if (std::find(kLoadedVSyncCallsites.begin(), kLoadedVSyncCallsites.end(), callsite) ==
        kLoadedVSyncCallsites.end()) {
      lucent::error("spyro2-loaded-boot",
                    "loaded payload contains unowned guest VSync callsite 0x{:08X}",
                    callsite);
      std::abort();
    }
    ++found;
  }
  if (found != kLoadedVSyncCallsites.size()) {
    lucent::error("spyro2-loaded-boot",
                  "loaded payload exposes {} of {} measured guest VSync callsites",
                  found,
                  kLoadedVSyncCallsites.size());
    std::abort();
  }
}

void Spyro2LoadedBootstrap::begin(Core &core) {
  if (begun_) {
    lucent::error("spyro2-loaded-boot", "loaded bootstrap entered twice");
    std::abort();
  }
  begun_ = true;
  validateRetainedTimingMap(core);

  // Retained 0x80077374 prologue, with the real boot-prefix return saved before interp_call uses a
  // measured timing PC as its private stop sentinel.
  core.r[29] -= 64u;
  core.mem_w32(core.r[29] + 52u, core.r[19]);
  core.r[19] = core.r[4];
  core.mem_w32(core.r[29] + 56u, core.r[31]);
  core.mem_w32(core.r[29] + 48u, core.r[18]);
  core.mem_w32(core.r[29] + 44u, core.r[17]);
  core.mem_w32(core.r[29] + 40u, core.r[16]);
  runUntil(core, 0x80077380u, 0x80077400u);

  const std::uint32_t snapshot = static_cast<std::uint32_t>(game_.presentation.fence());
  timing_.begin(snapshot);
  queryCounter(core, 0x80077408u, 0x80077424u);

  // JAL 0x800772A4's delay slot at 0x80077428.
  core.r[5] = core.r[4] + core.r[5];
  beginChild(core, 0x8007742Cu);
  lucent::info("spyro2-loaded-boot",
               "retained 0x80077374 reached its first child field at 0x800772F4 after host-owned "
               "VSync(-1) snapshot 0x80077400; all loaded non-timing instructions remain retained");
}

void Spyro2LoadedBootstrap::handleTransition(Core &core, LoadedBootstrapTransition transition) {
  switch (transition) {
  case LoadedBootstrapTransition::ChildAComplete:
    resumeChild(core, 0x800774E4u);
    queryCounter(core, 0x800774ECu, 0x80077504u);
    return;
  case LoadedBootstrapTransition::Threshold164Complete:
    runUntil(core, 0x8007750Cu, 0x800774E4u);
    queryCounter(core, 0x800774ECu, 0x8007751Cu);
    beginClear(core, 0x80077524u);
    return;
  case LoadedBootstrapTransition::ClearAComplete:
    resumeClear(core, 0x80077524u);
    return;
  case LoadedBootstrapTransition::FixedAComplete:
    runUntil(core, 0x8007752Cu, 0x80077534u);
    queryCounter(core, 0x8007753Cu, 0x8007755Cu);
    core.r[5] = core.r[3] + core.r[5]; // delay slot 0x80077560
    beginChild(core, 0x80077564u);
    return;
  case LoadedBootstrapTransition::ChildBComplete:
    resumeChild(core, 0x8007767Cu);
    queryCounter(core, 0x80077684u, 0x8007769Cu);
    return;
  case LoadedBootstrapTransition::Threshold60Complete:
    runUntil(core, 0x800776A4u, 0x8007767Cu);
    queryCounter(core, 0x80077684u, 0x800776C0u);
    core.r[5] = core.r[2] + core.r[5]; // delay slot 0x800776C4
    beginChild(core, 0x800776C8u);
    return;
  case LoadedBootstrapTransition::ChildCComplete:
    resumeChild(core, 0x800778C4u);
    queryCounter(core, 0x800778CCu, 0x800778E4u);
    return;
  case LoadedBootstrapTransition::Threshold180AComplete:
    runUntil(core, 0x800778ECu, 0x800778C4u);
    queryCounter(core, 0x800778CCu, 0x800778FCu);
    beginClear(core, 0x80077904u);
    return;
  case LoadedBootstrapTransition::ClearBComplete:
    resumeClear(core, 0x80077944u);
    return;
  case LoadedBootstrapTransition::FixedBComplete:
    runUntil(core, 0x8007794Cu, 0x80077954u);
    queryCounter(core, 0x8007795Cu, 0x80077980u);
    core.r[5] = core.r[2] + core.r[5]; // delay slot 0x80077984
    beginChild(core, 0x80077988u);
    return;
  case LoadedBootstrapTransition::ChildDComplete:
    resumeChild(core, 0x80077B74u);
    queryCounter(core, 0x80077B7Cu, 0x80077B98u);
    return;
  case LoadedBootstrapTransition::Threshold180BComplete:
    runUntil(core, 0x80077BA0u, 0x80077B74u);
    queryCounter(core, 0x80077B7Cu, kAfterLoadedBootstrap);
    lucent::info("spyro2-loaded-boot",
                 "retained loaded bootstrap 0x80077374 completed across {} host-owned fields; "
                 "zero guest VSync calls dispatched",
                 fieldsDelivered_);
    return;
  case LoadedBootstrapTransition::None:
  case LoadedBootstrapTransition::Invalid:
    break;
  }
  lucent::error("spyro2-loaded-boot", "invalid loaded-bootstrap timing transition");
  std::abort();
}

void Spyro2LoadedBootstrap::step(Core &core) {
  if (!begun_ || timing_.complete()) {
    lucent::error("spyro2-loaded-boot", "loaded bootstrap stepped outside its active lifetime");
    std::abort();
  }

  deliverField(core);
  const LoadedBootstrapTransition transition =
      timing_.consumeField(static_cast<std::uint32_t>(game_.presentation.fence()));
  if (transition == LoadedBootstrapTransition::Invalid) {
    lucent::error("spyro2-loaded-boot", "non-consecutive host field delivered to loaded bootstrap");
    std::abort();
  }
  if (transition != LoadedBootstrapTransition::None) {
    handleTransition(core, transition);
    return;
  }

  switch (timing_.phase()) {
  case Spyro2LoadedBootstrapTiming::Phase::ChildA:
  case Spyro2LoadedBootstrapTiming::Phase::ChildB:
  case Spyro2LoadedBootstrapTiming::Phase::ChildC:
  case Spyro2LoadedBootstrapTiming::Phase::ChildD:
    resumeChild(core, 0x800772F4u);
    return;
  case Spyro2LoadedBootstrapTiming::Phase::Threshold164:
    runUntil(core, 0x8007750Cu, 0x800774E4u);
    queryCounter(core, 0x800774ECu, 0x80077504u);
    return;
  case Spyro2LoadedBootstrapTiming::Phase::Threshold60:
    runUntil(core, 0x800776A4u, 0x8007767Cu);
    queryCounter(core, 0x80077684u, 0x8007769Cu);
    return;
  case Spyro2LoadedBootstrapTiming::Phase::Threshold180A:
    runUntil(core, 0x800778ECu, 0x800778C4u);
    queryCounter(core, 0x800778CCu, 0x800778E4u);
    return;
  case Spyro2LoadedBootstrapTiming::Phase::Threshold180B:
    runUntil(core, 0x80077BA0u, 0x80077B74u);
    queryCounter(core, 0x80077B7Cu, 0x80077B98u);
    return;
  case Spyro2LoadedBootstrapTiming::Phase::FixedA:
  case Spyro2LoadedBootstrapTiming::Phase::FixedB:
    return;
  case Spyro2LoadedBootstrapTiming::Phase::ClearA:
  case Spyro2LoadedBootstrapTiming::Phase::ClearB:
  case Spyro2LoadedBootstrapTiming::Phase::Complete:
    break;
  }
  lucent::error("spyro2-loaded-boot", "loaded bootstrap has no continuation for its timing phase");
  std::abort();
}

bool Spyro2LoadedBootstrap::complete() const {
  return timing_.complete();
}

Spyro2FrameDriver::Spyro2FrameDriver(Game &game) : display_(game), loadedBootstrap_(game) {}

void Spyro2FrameDriver::initialize(Core &core) {
  if (initialized_) {
    lucent::error("spyro2-boot", "Spyro 2 boot boundary initialized twice");
    std::abort();
  }
  initialized_ = true;

  // SCUS_944.25 game main 0x80011ADC owns this frame for process lifetime: it never returns from
  // its two-call frame loop. Preserve that exact frame instead of dispatching the non-returning
  // body.
  core.r[29] -= kGameMainFrameBytes;
  core.mem_w32(core.r[29] + 16u, core.r[31]);
  call(core, kStaticConstructors, kGameMainAfterConstructors);

  // Enter boot prefix 0x80011E9C only through the instructions before its first child. The next
  // child is display bootstrap 0x80011BBC, whose retained body has two direct VSync calls and a
  // third nested inside 0x8004C484. Enter its title-owned finite state machine instead of
  // dispatching either retained body.
  core.r[31] = kGameMainAfterBootPrefix;
  core.r[29] -= kBootPrefixFrameBytes;
  core.mem_w32(core.r[29] + 20u, core.r[31]);
  core.mem_w32(core.r[29] + 16u, core.r[16]);
  call(core, kBootPrefixFirstLeaf, kBootPrefixAfterFirstLeaf);
  core.r[31] = kBootPrefixAfterDisplay;
  display_.begin(core);
  reachedDisplayBootstrap_ = true;
  lucent::info("spyro2-boot",
               "native crt0/game-main boundary entered finite display bootstrap 0x{:08X}; "
               "retained retail body is available only for A/B",
               kDisplayBootstrap);
}

void Spyro2FrameDriver::stepFrame(Core &core, std::uint32_t) {
  if (!display_.complete()) {
    display_.step(core);
    if (!display_.complete()) {
      return;
    }

    // The retained 0x80011B1C body is a synchronous PsyQ SPU bootstrap: SpuInit, master-volume
    // setup, one all-voice ADSR template, key-off-all, and DMA transfer-mode selection. It owns no
    // display timing and all of its SPU-status polls are finite, so run it immediately after the
    // third display fence instead of inventing another host field or replacing binary-derived
    // audio state with a title-side approximation.
    call(core, kSpuBootstrap, kBootPrefixAfterSpuBootstrap);
    spuBootstrapComplete_ = true;

    // 0x80011B3C initializes libcd's callback slots, reads one 0x800-byte sector from LBA 500,
    // and copies its 0x628-byte boot-state prefix to 0x800676D8. Its measured CdInit/CdControl/
    // CdSync/CdRead leaves are bound to synchronous owners by Spyro2Runtime, including the exact
    // completion callback that terminates 0x80013810. It therefore owns no display field either.
    call(core, kCdBootstrap, kBootPrefixAfterCdBootstrap);
    cdBootstrapComplete_ = true;

    // 0x80012B84 only initializes the CD-streamed music controller consumed by the retained
    // per-frame 0x80012CBC state machine. It clears the controller phase/queues and seeds the
    // 0x3FFF/0x7FFF request sentinels; there are no calls, waits, or display-timing effects.
    call(core, kCdMusicInit, kBootPrefixAfterCdMusicInit);
    cdMusicInitialized_ = true;

    // 0x80011D24 is synchronous libgte geometry setup. Its retained InitGeom body enables COP2,
    // installs the stock GTE exception-vector support, and seeds the standard depth registers;
    // the two measured libgte setters then publish OFX=256, OFY=120, H=341 through the framework's
    // projection owner while making the same GTE control-register writes as the retail leaves.
    // None of these calls waits for or advances a display field.
    call(core, kGeometryInit, kBootPrefixAfterGeometryInit);
    geometryInitialized_ = true;

    // The next retail instructions pass four values produced by 0x80011B3C's LBA-500 header read
    // straight to the already-retained synchronous archive loader. On the US disc those inputs are
    // WAD base LBA 500, heap buffer 0x8006D264, size 0x12800, and byte offset 0x23000; keep reading
    // the guest-owned values rather than copying those observed contents into title policy.
    core.r[16] = kBootPayloadSize;
    core.r[4] = core.mem_r32(kArchiveBaseLba);
    core.r[5] = core.mem_r32(kArchiveBuffer);
    core.r[6] = core.mem_r32(kBootPayloadSize);
    core.r[7] = core.mem_r32(kBootPayloadOffset);
    call(core, kArchiveLoad, kBootPrefixAfterArchiveLoad);
    core.mem_w32(kLoadedPayloadSize, core.mem_r32(kBootPayloadSize));
    archiveLoaded_ = true;
    core.r[4] = 1u;
    core.r[31] = kAfterLoadedBootstrap;
    loadedBootstrap_.begin(core);
    lucent::info("spyro2-boot",
                 "retained synchronous SPU/CD/music/geometry bootstraps and archive load "
                 "0x{:08X}/0x{:08X}/0x{:08X}/0x{:08X}/0x{:08X} returned to boot prefix "
                 "0x{:08X}; no additional display field was delivered",
                 kSpuBootstrap,
                 kCdBootstrap,
                 kCdMusicInit,
                 kGeometryInit,
                 kArchiveLoad,
                 kBootPrefixAfterArchiveLoad);
    return;
  }
  if (!spuBootstrapComplete_ || !cdBootstrapComplete_ || !cdMusicInitialized_ ||
      !geometryInitialized_ || !archiveLoaded_) {
    lucent::error("spyro2-boot",
                  "display bootstrap completed without the synchronous SPU/CD/music/geometry/"
                  "archive boot state");
    std::abort();
  }
  if (!loadedBootstrap_.complete()) {
    loadedBootstrap_.step(core);
    return;
  }
  lucent::error("spyro2-boot",
                "Spyro 2 completed loaded bootstrap 0x80077374 without guest VSync and reached the "
                "next boot-prefix leaf 0x8001AAA4");
  std::abort();
}

bool Spyro2FrameDriver::reachedDisplayBootstrap() const {
  return reachedDisplayBootstrap_;
}

Spyro2FrameDriver &frameDriver(Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("spyro2-boot", "Spyro 2 runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<Spyro2FrameDriver &>(*core.game->frameDriver);
}

} // namespace spyro2
