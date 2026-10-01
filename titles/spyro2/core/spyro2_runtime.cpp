#include "spyro2_runtime.h"

#include "boot_prefix_frame_driver.h"
#include "cd_control.h"
#include "cd_stock_read_completion.h"
#include "core.h"
#include "frame_pacer.h"
#include "game.h"
#include "guest_cd_stream_callback_layout.h"
#include "spyro2_boot_facts.h"
#include "spyro_context.h"
#include "spyro_game.h"
#include "stock_read_publication.h"

#include <memory>

namespace spyro2 {
namespace {

// MEASURED from SCUS_944.25 with external/psxport/tools/disasm.py over a RAM image built the
// PS-X EXE way (file[0x800] -> t_addr 0x80010000). Each address is quoted with the bytes that
// give it its meaning, so a reader can re-derive it instead of trusting the table.

// libetc VSync. 37 `jal` sites reach it in the resident text, and its own body is the double
// buffered field wait: 0x80058EDC reads the two counter words through 0x80066450/0x80066454,
// spins until they differ, and returns the delta for a negative argument. The framework binds
// this address so the body never executes and a nonnegative wait becomes a typed frame-boundary
// exit the title's field owner satisfies.
constexpr std::uint32_t kVSync = 0x80058EDCu;

// The word SCUS_944.25's VSync returns for a negative argument: the guest's own vblank count, not a
// hardware register. The negative path (0x80058F34 `bgez $a0` not taken) falls to
//   80058F3C  lui $v0,0x8006 ; 80058F40  lw $v0,0x6618($v0)
// and that word is zeroed at 0x8005AC58 and incremented by the libetc vblank callback at
//   8005ACB0  addiu $v0,$v0,1 ; 8005ACB8  sw $v0,0x6618($at)
// The title's frame limiter (draw 0x80015900..0x80015990) compares it against its own saved last
// value, so answering from the 16-bit HBlank timer 0x1F801110 made `now - last < 2` hold for
// thousands of steps after the timer wrapped. See docs/issues/0160.
constexpr std::uint32_t kVSyncQueryCounter = 0x80066618u;

// libgpu's two projection leaves, called by the geometry-init leaf 0x80011D24:
//   80011D38  jal 0x80057AF8   with a0=0x100, a1=0x78
//   80011D40  jal 0x80057AE8   with a0=0x155
// 0x80057AF8 shifts both arguments left by 16 and returns through ra, and 0x80057AE8 is the same
// stub one word earlier: the offset pair, not the geometry value, is what identifies them.
constexpr std::uint32_t kSetGeomOffset = 0x80057AF8u;
constexpr std::uint32_t kSetGeomScreen = 0x80057AE8u;

// libcd's low-level command worker. 0x80058858 indexes a per-command table at 0x800663C8 with the
// command byte, calls the controller send at 0x8005CB80, and restores the sync callback it
// cleared at 0x80066700 around the wait: CdCommand(com, parm, result), the (com, param, result)
// signature the framework's stock command owner consumes.
constexpr std::uint32_t kCdCommand = 0x80058858u;

// libcd's synchronisation wait. 0x80058810 is called with a0=1 and polled until it returns 2
// (0x8001372C-0x80013738 and 0x80013778-0x80013784), which is CdSync(noblock) reporting CS_SELF.
constexpr std::uint32_t kCdSync = 0x80058810u;

// libcd's finite data read. 0x80058108 takes (dest, source, mode), derives the sector size from
// mode & 0x30 (0x80058198-0x800581B0: 0x200 / 0x249 / 0x246 bytes), and issues the read through
// the command owner: CdRead, the entry the framework's synchronous disc owner replaces.
constexpr std::uint32_t kCdRead = 0x80058108u;

// libcd's initialisation. 0x800582B8 is called by the CD bootstrap leaf 0x80011B3C before any
// command, retries a handshake, and reaches VSync(-1) at 0x8005824C. The controller has no
// interrupt handshake to complete here, so the owner's whole job is to report the state libcd
// publishes on success and let the guest continue.
constexpr std::uint32_t kCdInit = 0x800582B8u;

// The guest word libcd's CdReadyCallback writes. 0x800582A4 is `*(0x800663B8) = a0` returning the
// previous value, and the CD bootstrap leaf 0x80011B68 calls it with the image's own per-sector
// callback 0x8001379C, so this is the slot the stock CD path must read to find that callback.
constexpr std::uint32_t kCdReadyCallbackPointer = 0x800663B8u;

// libcd's own private handshake state, published by CdInit so the guest's callback and the
// command worker agree on who runs next. 0x800582B8's success path writes each of these through
// the same setters the guest uses: 0x800582A4 (ready callback), 0x80058830 (0x80066700, the sync
// callback CdCommand saves and restores) and 0x80058844 (0x80066704).
constexpr std::uint32_t kCdSyncCallbackPointer = 0x80066700u;
constexpr std::uint32_t kCdReadyCallbackSlot = 0x80066704u;
constexpr std::uint32_t kCdInitFlag = 0x800663BCu;
constexpr std::uint32_t kCdInitReadyCallback = 0x800583D4u;
constexpr std::uint32_t kCdInitSyncCallback = 0x80058384u;
constexpr std::uint32_t kCdInitStreamCallback = 0x800583ACu;

// libgpu's DrawSync, and the two leaves of its measured timeout. The display bootstrap leaf
// 0x80011BBC calls 0x8005574C, 0x8005557C and 0x800556F0 in sequence after its VSync, and the
// old finite bootstrap named the DrawSync timeout globals 0x80066354/0x80066358 in this image;
// the host GPU consumes GP0 and DMA work synchronously, so every one of those waits completes
// without a display field.
constexpr std::uint32_t kGpuTimeoutDeadline = 0x80066354u;
constexpr std::uint32_t kGpuTimeoutPollCount = 0x80066358u;
constexpr std::uint32_t kGpuTimeoutArm = 0x80057B20u;
constexpr std::uint32_t kGpuTimeoutCheck = 0x80057AF4u;

void cdInitSuccess(Core *core) {
  // 0x800582B8's success path leaves libcd's callback table and its initialised flag in a state
  // the guest's own command worker reads. Publishing the same four words is the whole native
  // contract: there is no controller handshake left to fail, and pretending otherwise would
  // leave the guest's per-sector callback unregistered.
  core->mem_w32(kCdReadyCallbackPointer, kCdInitReadyCallback);
  core->mem_w32(kCdInitFlag, 0u);
  core->mem_w32(kCdSyncCallbackPointer, kCdInitSyncCallback);
  core->mem_w32(kCdReadyCallbackSlot, kCdInitStreamCallback);
  // Retail CdInit also opens the CD-ROM interrupt line in I_MASK; this body replaces it, so it owes
  // the same effect, or the completion a stock read queues is never deliverable (framework owner).
  psx::cd::armCdInterrupt(*core);
  core->r[2] = 1u;
}

void completeDrawSync(Core *core) {
  // The host GPU consumes guest GP0/DMA work synchronously, so DrawSync has no pending work and
  // returns success without spending a display field or entering its retail timeout loop.
  core->r[2] = 0u;
}

void armGpuTimeout(Core *core) {
  // Retail derives this deadline from VSync(-1). The host GPU is synchronous, so it can never
  // reach the display-clock timeout: the same far-future deadline the framework's synchronous
  // GPU owner uses, and the measured poll-count global reset with it.
  core->mem_w32(kGpuTimeoutDeadline, 0x7FFFFFFFu);
  core->mem_w32(kGpuTimeoutPollCount, 0u);
}

} // namespace

const GuestProgramImage Spyro2Runtime::programImage_{
    .bss = {0x80066ED8u, 0x8006D264u},
    .stackTopWordAddress = 0x80066D3Cu,
    .stackReserveWordAddress = 0x80066D38u,
    .heapBase = 0x8006D264u,
    .heapSizeStoreAddress = 0x8006509Cu,
    .heapBaseStoreAddress = 0x80065098u,
    .globalPointer = 0x80066D38u,
    .libcInitEntry = 0x8005ABD8u,
    .gameMainEntry = 0x80011ADCu,
    .crt0Entry = 0x8005478Cu,
    .residentText = {0x00010000u, 0x00067000u},
    .backtraceText = {},
    .stackBias = {true, -8},
};

const PlatformHlePlan Spyro2Runtime::platformHlePlan_{
    .setGeomOffset = kSetGeomOffset,
    .setGeomScreen = kSetGeomScreen,
    .cdReadAddress = kCdRead,
    .cdCommandAddress = kCdCommand,
    .cdSyncAddress = kCdSync,
    .vsyncAddress = kVSync,
    .vsyncQueryCounterAddress = kVSyncQueryCounter,
    .bindings = {{kCdInit, cdInitSuccess},
                 {kGpuTimeoutArm, armGpuTimeout},
                 {kGpuTimeoutCheck, completeDrawSync}},
    .bindingCount = 3,
    // Three windows, because the framework admits four and every address this title binds lies in
    // one of these three regions of the image:
    //   libetc  0x80058EDC  VSync
    //   libgpu  0x80057AE8  SetGeomScreen, and the GPU timeout pair at 0x80057AF4/0x80057B20
    //   libcd   0x80058108  CdRead, 0x800582B8 CdInit, 0x80058810 CdSync, 0x80058858 CdCommand
    // The libcd window is one range because its five measured leaves are 4 KB apart and the
    // framework's capacity is four slots; it covers only libcd, and the game's own code ends
    // before 0x80050000.
    .windowLo = {kVSync, kSetGeomScreen, kCdRead},
    .windowHi = {0x80059054u, 0x80057BA0u, 0x8005CA00u},
};

const GuestCdStreamCallbackLayout Spyro2Runtime::cdStreamCallbackLayout_{
    .readyCallbackPointer = kCdReadyCallbackPointer,
    .owner = GuestCdStreamCallbackLayout::DeliveryOwner::GuestInterrupt,
    // The loader's ready callback (0x8001379C) ends a read when `$a0 & 0xFF == 2` (libcd's
    // completion code) and otherwise issues the next read, and it chains one-sector reads from that
    // callback.
    .readyStatus = 2,
    .stockReadRaisesCompletion = true,
};

Spyro2Runtime::Spyro2Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro2) {}

void *Spyro2Runtime::createContext(Core &) {
  // The shared lineage context: the field owner back-pointer, the run counter, and the archive
  // transfer that serves this title's WAD reads. None of its render histories are written on
  // this title's route, which is what makes it a shared owner rather than Spyro 1's state.
  return new SpyroContext();
}

void Spyro2Runtime::destroyContext(void *context) {
  delete static_cast<SpyroContext *>(context);
}

void Spyro2Runtime::registerOverrides(Game &) {
  // Every Spyro 2 hardware service this title's boot reaches is either a measured library leaf
  // in the plan above or the framework's own stock CD path. A title override installed here
  // would be a claim about SCUS_944.25's own code, and boot has not yet produced evidence for
  // one; the log says so rather than implying a set was installed.
  lucent::info("boot",
               "installed no Spyro 2 native overrides: every boot service is a measured "
               "library leaf or the framework's stock CD seam");
}

void Spyro2Runtime::bootInit(Core &core) {
  spyro::bootPrefixFrameDriver(core).initialize();
}

std::unique_ptr<FrameDriver> Spyro2Runtime::createFrameDriver(Game &game) {
  return std::make_unique<spyro::BootPrefixFrameDriver>(game, kBootPrefixFacts);
}

const PlatformHlePlan *Spyro2Runtime::platformHlePlan() const {
  return &platformHlePlan_;
}

const char *Spyro2Runtime::discEnvVar() const {
  return "PSXPORT_SPYRO2_DISC";
}

const GuestCdStreamCallbackLayout *Spyro2Runtime::guestCdStreamCallbackLayout() const {
  return &cdStreamCallbackLayout_;
}

void Spyro2Runtime::pacePresentation(Core &core, int fields, int parts) {
  // The field owner has already delivered this simulated time, including the guest vblank work
  // that came with it, so presentation only has to wait out the host deadline.
  gpu_wait_presented_fields(&core, fields, parts);
}

void Spyro2Runtime::stockCdReadLanded(Core &core, const psx::cd::StockReadLanding &landing) {
  // The loader streams its code modules through the stock read and then calls into them.
  spyro::publishStockReadLanding(core, landing);
}

bool Spyro2Runtime::guestVramIsPicture(const Game &) const {
  // Spyro 2 has no native producer: every presented field is guest VRAM, from the display
  // bootstrap's own clear through the title screen the guest draws.
  return true;
}

} // namespace spyro2
