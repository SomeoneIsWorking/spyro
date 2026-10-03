#include "spyro3_runtime.h"

#include "boot_prefix_frame_driver.h"
#include "cd_stock_read_completion.h"
#include "core.h"
#include "frame_pacer.h"
#include "game.h"
#include "spyro3_boot_facts.h"
#include "spyro3_hud_anchor.h"
#include "spyro3_logo_facts.h"
#include "spyro3_render_facts.h"
#include "spyro3_widescreen_facts.h"
#include "spyro_context.h"
#include "stock_read_publication.h"

#include <cstdlib>
#include <lucent/log.h>
#include <memory>

namespace spyro3 {
namespace {

// MEASURED from SCUS_944.67 with external/psxport/tools/disasm.py over a RAM image built the
// PS-X EXE way (`file[0x800] -> t_addr 0x80010000`, the only mapping under which `0x8001xxxx` holds
// code). Every address below is quoted with the instruction bytes that give it its meaning, so a
// reader can re-derive it instead of trusting the table. The Spyro 2 plan in
// `titles/spyro2/core/spyro2_runtime.cpp` is the structural twin of this one because both images
// link the same SCEI libraries -- but each address here was located in THIS image's bytes, and the
// one place the twins disagree is called out below.

// The PS-X EXE header, from the identity manifest and confirmed by the crt0 that consumes it:
//   0x10 entry 0x80059444   0x18 t_addr 0x80010000   0x1C t_size 0x0005C800
// The crt0 clears exactly the declared bss span and then derives `$sp` from the declared stack top:
//   80059444  lui $v0,0x8007 ; addiu $v0,$v0,-0x3b0c   -> 0x8006C4F4, the bss low
//   8005944C  lui $v1,0x8007 ; addiu $v1,$v1,0x42d0    -> 0x800742D0, the bss high (the loop bound)
//   8005946C  lw  $v0,-0x3c1c($v0)                      -> reads 0x8006C3E4, stackTopWordAddress
//   80059474  addi $v0,$v0,-8 ; or $sp,$v0,$t0          -> the -8 bias
//   `GuestProgramImage::stackBias`
const GuestProgramImage kProgramImage{
    .bss = {0x8006C4F4u, 0x800742D0u},
    .stackTopWordAddress = 0x8006C3E4u,
    .stackReserveWordAddress = 0x8006C3E0u,
    .heapBase = 0x800742D0u,
    .heapSizeStoreAddress = 0x80069F04u,
    .heapBaseStoreAddress = 0x80069F00u,
    .globalPointer = 0x8006C3B0u,
    .libcInitEntry = 0x8005F63Cu,
    .gameMainEntry = 0x8001200Cu,
    .crt0Entry = 0x80059444u,
    .residentText = {0x00010000u, 0x0006C800u},
    .backtraceText = {},
    .stackBias = {true, -8},
};

// libetc VSync. 36 `jal` sites reach it in the resident text, and its own body is the double
// buffered field wait:
//   8005956C  lui $v0,0x8007 ; 80059570 lw $v0,-0x60d8($v0)   the mode-0 counter POINTER
//   80059574  lui $a1,0x8007 ; 80059578 lw $a1,-0x60d4($a1)   the mode-1 counter POINTER
//   8005958C  lw  $s0,($v0)                                     the mode-0 count on entry
//   80059590  lw  $v0,($a1) ; 800595A8 bne $v1,$v0,...          poll the mode-1 count until it
//   moves 800595C0  subu $v0,$v0,$v1                                  minus the count at 0x80069F30
//   800595C4  bgez $a0,0x800595dc ; 800595E0 beq $a0,1 ; 800595E8 blez $a0
// so a nonnegative argument waits and only a negative one returns a count. The framework binds
// this address, which is why the library body never executes and a wait comes back as a typed
// frame-boundary exit.
constexpr std::uint32_t kVSync = 0x8005956Cu;

// The word a negative VSync returns: the guest's own vblank count maintained by the libetc vblank
// callback, not a hardware register. VSync's negative path (0x800595C4 `bgez $a0` not taken) is
//   800595CC  lui $v0,0x8007 ; 800595D0  lw $v0,-0x4b80($v0)     -> 0x8006B480
// The earlier reading of 0x80069F2C/0x1F801110 described the blocking-wait pointer pair, not this
// query. See docs/issues/0160.
constexpr std::uint32_t kVSyncQueryCounter = 0x8006B480u;

// libgpu's projection offset leaf, called by this image's geometry-init leaf 0x8002A99C with the
// same (0x100, 0x78) pair Spyro 2's uses, and by nothing else in the image:
//   8002A9AC  addiu $a0,$zero,0x100 ; 8002A9B4 addiu $a1,$zero,0x78
//   8002A9B0  jal  0x8005D35C
//   8005D35C  sll $a0,$a0,0x10 ; 8005D360 sll $a1,$a1,0x10
//   8005D364  48c4c000 ; 8005D368 48c5c800              two dead COP2 reads, then
//   8005D36C  jr $ra
// The two shifts are the whole live body, and the framework's handler performs exactly those two
// shifts -- so every register the guest can still read comes out the same -- and then writes the
// GTE geometry offset the two dead reads would have needed. That is a superset, not a divergence.
//
// NOT DECLARED, and the reason is measured rather than omitted. The companion call one instruction
// later is `jal 0x8005955C` with $a0 = 0x155 (0x8002A9B8), and 0x8005955C is a three-word leaf
// whose body is `48c4d000` (a COP2 register read) then `jr $ra`. It is NOT SetGeomScreen: the same
// address is called from game main at 0x8001206C with a COMPUTED argument, `lw $a0,-0x1e30($a0)`
// then `addiu $a0,$a0,0x155`, which no fixed-arity geometry leaf takes. Binding the framework's
// projection writer there would both mis-name the leaf and put GTE state where retail's own call
// puts none, so `setGeomScreen` stays 0 and the guest's three words keep running.
constexpr std::uint32_t kSetGeomOffset = 0x8005D35Cu;

// libcd's finite data read. Structural twin of the Spyro 2 leaf, located here by its own bytes:
//   8005D96C  addiu $sp,$sp,-0x28 ; sw $s4,0x20($sp) ; move $s4,$a0     (dest)
//   8005D978  move $s3,$a1 (src) ; 8005D9A4 move $s2,$a2 (mode)
//   8005D984  lui $s0,0x8007 ; addiu $s0,$s0,-0x4c4c   the stock libcd work area at 0x8006B3B4
//   8005D9A8  jal  0x8005956C   with $a0 = -1 : the VSync query the read's timeout is built on
//   8005D9FC  andi $v1,$a2,0x30                       the sector-size selector, mode & 0x30
// CdRead(dest, src, mode), the entry the framework's synchronous disc owner replaces.
constexpr std::uint32_t kCdRead = 0x8005D96Cu;

// libcd's synchronisation wait, the leaf this image's own loader polls. The loader is the CD
// bootstrap leaf 0x80050440 and its poll is unambiguous:
//   80050490  addiu $a0,$zero,1 ; 80050498 move $a1,$zero
//   80050494  jal  0x8005E074
//   8005049C  bne $v0,$s0,0x80050494   with $s0 = 2 (set at 0x8005048C)
// i.e. CdSync(1) polled until it reports CS_SELF. 0x8005E074 is the thin entry
// (`addiu $sp,$sp,-0x18 ; sw $ra ; jal 0x8006143C ; lw $ra ; jr $ra`) over the wait body.
constexpr std::uint32_t kCdSync = 0x8005E074u;

// libcd's low-level command worker, CdCommand(com, parm, result):
//   8005E0BC  addiu $sp,$sp,-0x38 ; move $s1,$a1 ; move $s2,$a2 ; move $s4,$a0
//   8005E0E4  andi $s3,$s4,0xff                            the command byte
//   8005E0E8  lui $v1,0x8007 ; addiu $v1,$v1,-0x4c20        the per-command table at 0x8006B3E0
//   8005E0F4  lw  $s5,-0x4a98($s5)                          the sync callback at 0x8006B568
// The CD bootstrap leaf reaches it directly at 0x8002A7D0 with $a0 = 0x0E.
constexpr std::uint32_t kCdCommand = 0x8005E0BCu;

// libcd's initialisation, CdInit at 0x8005DB1C (called by the CD bootstrap leaf 0x8002A7B4 at
// 0x8002A7C0), is NOT bound: the retail body runs through Lightrec. It registers libcd's
// interrupt handler (CD_init 0x80061C8C -> InterruptCallback(2, 0x800621CC)), which is what the
// libapi dispatcher 0x8005C6B4 needs to acknowledge the CD interrupt; a native stand-in that only
// wrote the callback words left it unregistered and every later CD interrupt ended in the
// "intr timeout" at 0x8005CA18. See docs/issues/0159.

// The guest word libcd's CdReadyCallback reads and writes (CdInit's 8005DB6C stores the default
// ready callback 0x8005DC38 there through `lui $at,0x8007 ; sw $v1,-0x4c30($at)`), which the stock
// CD path reads to find the title's per-sector callback.
constexpr std::uint32_t kCdReadyCallbackPointer = 0x8006B3D0u;

} // namespace

const GuestProgramImage Spyro3Runtime::programImage_ = kProgramImage;

const PlatformHlePlan Spyro3Runtime::platformHlePlan_{
    .setGeomOffset = kSetGeomOffset,
    .setGeomScreen = 0,
    .cdReadAddress = kCdRead,
    .cdCommandAddress = kCdCommand,
    .cdSyncAddress = kCdSync,
    .vsyncAddress = kVSync,
    .vsyncQueryCounterAddress = kVSyncQueryCounter,
    .bindings = {},
    .bindingCount = 0,
    // Three windows, because the framework admits four and every address this title binds lies in
    // one of these three regions of the image:
    //   libetc  0x8005956C  VSync, and its timeout helper at 0x800596E4
    //   libgpu  0x8005D35C  SetGeomOffset
    //   libcd   0x8005D96C  CdRead, 0x8005DB1C CdInit, 0x8005E074 CdSync, 0x8005E0BC CdCommand
    // The libcd window is one range because its four measured leaves are 2.5 KB apart and the
    // framework's capacity is four slots. It covers only libcd: the game's own code ends below
    // 0x80058000, and the leaves below the window are left executing on purpose -- CdReadyCallback
    // 0x8005DB08 because it is a plain pointer setter with no hardware service in it, and the
    // GPU timeout pair 0x8005C2FC (which arms a deadline of VSync(-1) + 0xF0 at 0x8006A25C) and
    // 0x8005C330 (which tests it) because the host GPU consumes guest work synchronously, so the
    // guest's own arm and check complete without spending a display field and the host has no
    // reason to own two guest words to say so.
    .windowLo = {kVSync, kSetGeomOffset, kCdRead},
    .windowHi = {0x80059800u, 0x8005D400u, 0x8005E200u},
};

const GuestCdStreamCallbackLayout Spyro3Runtime::cdStreamCallbackLayout_{
    .readyCallbackPointer = kCdReadyCallbackPointer,
    .owner = GuestCdStreamCallbackLayout::DeliveryOwner::GuestInterrupt,
    // The per-sector reader (0x80050504) ends a read when `$a0 & 0xFF == 2` (libcd's completion
    // code) and otherwise starts the next one, so it is delivered 2, once per stock CdRead (psxport
    // issue 0143).
    .readyStatus = 2,
    .stockReadRaisesCompletion = true,
};

Spyro3Runtime::Spyro3Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro3) {}

void *Spyro3Runtime::createContext(Core &) {
  // The shared lineage context: the field owner back-pointer, the run counter, and the archive
  // transfer that serves this title's WAD reads. None of its render histories are written on this
  // title's boot route, which is what makes it a shared owner rather than Spyro 1's state.
  return new SpyroContext();
}

void Spyro3Runtime::destroyContext(void *context) {
  delete static_cast<SpyroContext *>(context);
}

void Spyro3Runtime::registerOverrides(Game &game) {
  // The libgte projection leaf this title's own geometry init states its 512-dot window through,
  // and through it the title's widening decision; the two hand-written render routines whose
  // horizontal culls would otherwise cull everything the widening reveals -- the moby visibility
  // walk and the terrain drawer; and the HUD emitters, whose edge widgets would otherwise ride the
  // widened centre instead of holding their distance from the frame's edges. Every OTHER hardware
  // service this title's boot reaches is either a measured library leaf in the plan above or the
  // framework's own stock CD path.
  spyro3::hud_anchor::registerOverrides(game.core);
  widescreen_.registerProjectionOverrides(game.core);
  registerRenderOverrides(game.core);
  lucent::info("boot",
               "installed Spyro 3 native overrides for the measured libgte projection offset leaf, "
               "the moby visibility walk, the terrain drawer and the HUD anchor; every other boot "
               "service remains a measured library leaf or the framework's stock CD seam");
}

void Spyro3Runtime::bootInit(Core &core) {
  spyro::bootPrefixFrameDriver(core).initialize();
}

std::unique_ptr<FrameDriver> Spyro3Runtime::createFrameDriver(Game &game) {
  // The widescreen owner is this title's field observer AND its frame-tail hook, for the two
  // measured reasons spyro2_runtime.cpp states: the widened horizontal centre must be in CR24
  // before the guest resumes drawing, and the GPU drawing rectangle must be widened after the
  // guest's last command and before the queue rasterises.
  return std::make_unique<spyro::BootPrefixFrameDriver>(
      game, kBootPrefixFacts, &widescreen_, &widescreen_);
}

const PlatformHlePlan *Spyro3Runtime::platformHlePlan() const {
  return &platformHlePlan_;
}

const char *Spyro3Runtime::discEnvVar() const {
  return "PSXPORT_SPYRO3_DISC";
}

const GuestCdStreamCallbackLayout *Spyro3Runtime::guestCdStreamCallbackLayout() const {
  return &cdStreamCallbackLayout_;
}

const GuestWidescreenProjection *Spyro3Runtime::guestWidescreenProjection() const {
  return &widescreen_;
}

void Spyro3Runtime::pacePresentation(Core &core, int fields, int parts) {
  // The field owner has already delivered this simulated time, including the guest vblank work
  // that came with it, so presentation only has to wait out the host deadline.
  gpu_wait_presented_fields(&core, fields, parts);
}

void Spyro3Runtime::stockCdReadLanded(Core &core, const psx::cd::StockReadLanding &landing) {
  // The loader streams its code modules through the stock read and then calls into them.
  spyro::publishStockReadLanding(core, landing);
}

bool Spyro3Runtime::guestVramIsPicture(const Game &) const {
  // Spyro 3 has no native producer: every presented field is guest VRAM, from the display
  // bootstrap's own clear through whatever the guest draws.
  return true;
}

const spyro::TitleLogoFacts &Spyro3Runtime::logoFacts() const {
  return kLogoFacts;
}

} // namespace spyro3
