// game_config.cpp — the Spyro-specific GameConfig instance (psxport's game_iface.h seam).
//
// Every value here is a guest-address literal from Spyro the Dragon's executable (SCUS_942.28). The
// PSX-generic framework reads `c->cfg->field` wherever it would otherwise need a game constant, so
// this file is the ONE place those addresses live.
//
// PROVENANCE RULE: every number below is derived from the binary and carries the disassembly that
// justifies it. A guest address that is GUESSED silently breaks boot or diverges the byte-compare in
// a way that looks like a framework bug, so an un-RE'd field stays 0 with an explicit TODO rather
// than being filled with a plausible-looking value. Zero is honest; a wrong address is not.
//
// Installed once at the top of main() (game/core/main.cpp), before any Game/Core is constructed —
// Core's ctor snapshots psxport_game_config() into c->cfg.
#include "game_iface.h"
#include "overlay_table.h"   // generated: REC_MAIN_LO/HI — our recompiler run's own text range
#include "spyro_game.h"      // spyro_game_hooks()

static const GameConfig g_spyro_config = {
  // ── crt0 / boot ────────────────────────────────────────────────────────────────────────────────
  // Spyro's crt0 IS the PS-EXE entry point, 0x8005B8E0, and it is a textbook Sony crt0 — the
  // framework's generic crt0_setup() reproduces it instruction for instruction. Disassembly:
  //
  //   8005B8E0  lui v0,0x8007 ; addiu v0,v0,0x5640     v0 = 0x80075640   ─┐ .bss clear:
  //   8005B8E8  lui v1,0x8008 ; addiu v1,v1,-0x55c8    v1 = 0x8007AA38    │ for (a=lo; a<hi; a+=4)
  //   8005B8F0  sw zero,(v0) ; addiu v0,v0,4 ; sltu ; bnez                ┘   *a = 0
  //   8005B904  lui v0,0x8007 ; lw v0,0x55a8(v0)       v0 = [0x800755A8]  ─┐ stack top
  //   8005B910  addi v0,v0,-8 ; or sp,v0,0x80000000    sp = (v0-8)|KSEG0  ┘
  //   8005B91C  a0 = 0x8007AA38, masked to 29 bits (sll 3 / srl 3)          heap base
  //   8005B92C  lui v1,0x8007 ; lw v1,0x55a4(v1)       v1 = [0x800755A4]
  //   8005B938  subu a1,v0,v1 ; subu a1,a1,a0          heapsz = (v0-v1)-a0
  //   8005B944  sw a1,0x30c4(at)                       [0x800730C4] = heap size
  //   8005B950  sw a0,0x30c0(at)                       [0x800730C0] = heap base
  //   8005B95C  lui gp,0x8007 ; addiu gp,gp,0x5264     gp = 0x80075264
  //   8005B968  jal 0x8005DB14  (delay: addi a0,a0,4)  libc/heap init, a0 = heapBase+4
  //   8005B97C  jal 0x80012204                         main()  — never returns (break follows)
  //
  // Note bssZeroLo (0x80075640) sits just below the PS-EXE's text_end (0x80075800): the header's
  // text_size is 2048-aligned, so the tail of the image is padding and .bss starts inside it.
  /* bssZeroLo      */ 0x80075640u,
  /* bssZeroHi      */ 0x8007AA38u,
  /* stackTopBase   */ 0x800755A8u,   // sp   = [this] - 8
  /* stackTopBase2  */ 0x800755A4u,   // heapsz = (sp+8-8) - [this] - heapBase
  /* heapBase       */ 0x8007AA38u,   // == bssZeroHi (heap starts where .bss ends)
  /* heapSizePtr    */ 0x800730C4u,
  /* heapBasePtr    */ 0x800730C0u,
  /* gp             */ 0x80075264u,
  /* libcInit       */ 0x8005DB14u,
  /* gameMain       */ 0x80012204u,   // main(), tail-called by crt0
  /* crt0           */ 0x8005B8E0u,   // == the PS-EXE entry point

  // Recompiled .text range (physical, addr & 0x1FFFFFFF), taken from our own recompiler run via
  // generated/overlay_table.h so it can never drift from the substrate it describes.
  /* recMainLo      */ REC_MAIN_LO,
  /* recMainHi      */ REC_MAIN_HI,

  // ── per-frame OT / packet-pool dance ───────────────────────────────────────────────────────────
  // NOT YET REVERSE-ENGINEERED. These drive the framework's NATIVE per-frame loop
  // (native_step_frame): the double-buffered ordering-table + packet-pool addresses the game's
  // display code uses. Phase 0 runs the whole guest under the substrate (see game_hooks.cpp
  // spyro_bootInit), which uses the game's OWN display code and never reads these — so leaving them
  // 0 is correct and load-bearing-free TODAY, and filling them with guesses would be exactly the
  // "fake the output before the RE is done" failure the porting playbook warns about.
  // RE these when moving to the native frame loop: find Spyro's display init (SetDefDrawEnv/
  // SetDefDispEnv callers) and its per-frame buffer flip.
  /* otRegionBase     */ 0u,
  /* otRegionStride   */ 0u,
  /* packetPoolBase   */ 0u,
  /* packetPoolStride */ 0u,
  /* otBasePtr        */ 0u,
  /* dwellCounter     */ 0u,
  /* poolPtrCur       */ 0u,
  /* poolPtrLast      */ 0u,
  /* clearOtagR       */ 0u,
  /* putDrawEnv       */ 0u,
  /* drawSync         */ 0u,
  // Per-frame IRQ-driven event classes the guest's waits poll via TestEvent. DISCOVERED from a run,
  // not guessed: PSXPORT_DEBUG=ev logs `OpenEvent class=0xF0000009 spec=0x20 -> handle=0xF1000000`,
  // and func_8005CBB0 then polls exactly that handle forever (claim C022). One class is opened, so
  // the other two slots stay 0 — deliverEvent(0) is a no-op.
  /* irqEventClasses  */ { 0xF0000009u, 0u, 0u },
  /* dualviewRenderOrch */ 0u,
  /* dualviewSubmit     */ 0u,

  // ── scheduler task layout ──────────────────────────────────────────────────────────────────────
  // NOT APPLICABLE AS-IS. These describe Tomba!2's cooperative task table and its three stage-entry
  // PCs (START/DEMO/GAME overlays). Spyro is a SINGLE executable with no overlays and no such stage
  // split, so there is nothing to point them at. They stay 0 until (and unless) Spyro's own main
  // loop structure is RE'd and shown to need them.
  /* taskTableBase  */ 0u,
  /* taskSlotStride */ 0u,
  /* taskCount      */ 0u,
  /* curTaskPtr     */ 0u,
  /* stageStart     */ 0u,
  /* stageDemo      */ 0u,
  /* stageGame      */ 0u,

  // ── overlay router slots ───────────────────────────────────────────────────────────────────────
  // UNRESOLVED — see docs/issues/0001. The disc carries no per-overlay FILES (its whole tree is
  // SYSTEM.CNF, SCUS_942.28, WAD.WAD, SOURCE/, S0/, PETEXA*.STR), so there is nothing for the router
  // to point at today. But public decomp projects describe Spyro as the main EXE plus 37 overlays,
  // which would then live inside WAD.WAD or be read by raw LBA. Do NOT read these zeros as "Spyro has
  // no overlays" — read them as "no overlay load base has been OBSERVED yet". The way to settle it is
  // a running port: PSXPORT_DEBUG=cd logs each load destination, and an unresolved call fail-fasts
  // with its address.
  /* overlaySlots */ {
    { 0u, nullptr },
    { 0u, nullptr },
    { 0u, nullptr },
  },

  // ── CD chokepoints ─────────────────────────────────────────────────────────────────────────────
  // NOT YET REVERSE-ENGINEERED. The framework's native CD path overrides these guest functions to
  // serve reads from the CHD directly. Until they are RE'd, CD access runs through the recompiled
  // libcd code on the substrate. RE these by finding the CdReadFile/CdlSetloc call sites that pull
  // from WAD.WAD.
  /* cdInit            */ 0u,
  // CD_cw (0x80064CEC) — libcd's command-issue-and-wait. SIGNATURE CONFIRMED by reading the body, not
  // inferred from the name: the prologue keeps a0 in r16 and uses `r16 & 255` to index the command
  // tables at 0x800750xx, keeps a1 in r17 (tested against 0 = "no param"), a2 in r21 (result) and a3
  // in r18 — i.e. CD_cw(com, param, result, mode), exactly what the framework's cd_command handler
  // reads. This is the function the boot log shows timing out on the real commands (CdlSetmode,
  // CdlSetloc), so overriding it ACKs those commands instead of spinning on a controller we don't model.
  /* cdCommand         */ 0x80064CECu,
  // CD_sync (0x800647A0). SIGNATURE NOW CONFIRMED from the body — the prologue keeps a0 in r21 and
  // a1 in r22, and a1 is used as the result buffer, matching the framework's cd_sync handler
  // (zero 8 bytes at a1, return 2 = complete). It was left 0 until this check was actually done.
  //
  // Why it spins: CD_sync polls the drive by calling VSync(-1) (func_8005DBC4 with a0 = -1) in a loop,
  // waiting on a ready flag that only a CD IRQ would set. With reads served natively and synchronously
  // there is nothing to wait for, so reporting "complete" is the faithful answer. A 6-sample stack
  // profile put the guest squarely in this function.
  /* cdSync            */ 0x800647A0u,
  /* cdReadPrim        */ 0u,
  /* cdFileLoad        */ 0u,
  /* cdAsyncRead       */ 0u,
  /* voicePlay         */ 0u,
  /* voiceStop         */ 0u,
  /* lastSectorTracker */ 0u,
  /* cdInlineLoad      */ 0u,
  /* cdCmdStream       */ 0u,
  /* cdCallbackTable   */ { 0u, 0u, 0u, 0u },
  /* cdCallbackFn      */ { 0u, 0u, 0u, 0u },

  // ── pad driver ─────────────────────────────────────────────────────────────────────────────────
  // NOT YET REVERSE-ENGINEERED. Input runs through the guest's own SIO pad read until these are
  // found (the per-VBlank pad buffer + the driver entry that fills it).
  /* padSlot0Buf   */ 0u,
  /* padSlot1Buf   */ 0u,
  /* padDriverFn   */ 0u,
  /* padSlotPtrTable */ 0u,

  // ── platform HLE: the PSX hardware-sync primitives ─────────────────────────────────────────────
  // NOT YET REVERSE-ENGINEERED — every entry 0, which initBuiltins() reads as "this game has no such
  // primitive, or its RE is outstanding" and skips. Spelled out explicitly rather than left to
  // trailing zero-init, because a zero here is a CLAIM about Spyro and should be visible as one.
  //
  // Consequence, stated plainly: with no window configured, register_() refuses every registration
  // and says so, and no sync primitive is installed. The guest therefore spins in the REAL library
  // spin loops — which is exactly the honest signal that this RE is outstanding, and is the shape of
  // the CD timeouts seen at boot (docs/re-frontier.md cd.chokepoints).
  //
  // vsyncTrap stays 0 and MUST stay 0 for now: the trap encodes the policy "nothing may reach VSync
  // because the native frame loop owns all timing", which is only true once that loop drives the
  // frame. Spyro still runs the guest's own loop on the substrate (see game_hooks.cpp), so VSync must
  // be reimplemented faithfully and registered by us — setting the trap instead would abort on the
  // game's own legitimate timing.
  /* hle */ {
    // Window 0 — Spyro's libcd. Deliberately TIGHT: it spans only the region where the libcd bodies
    // were actually located (the five functions that reference the "CD_cw"/"CD timeout"/"CD_init"
    // strings live at 0x80063C48..0x800655A0), not a guessed "SDK region". The window exists to keep
    // game logic out of this table, so it is widened only as more library code is genuinely RE'd.
    // Window 1 is free for libgpu/libmdec once those are located.
    // Window 1 — libc/libetc. Anchored on crt0 (0x8005B8E0), the lowest library address confirmed
    // so far, and stopping where window 0 begins. It covers the libetc vblank machinery
    // (VSync 0x8005DBC4, its wait helper 0x8005DD0C) and libcInit 0x8005DB14. Spyro's link order
    // puts game code low (main is at 0x80012204) and the Sony libraries high, so this does not
    // overlap game logic — which is the property the window exists to guarantee.
    /* windowLo */ { 0x80063000u, 0x8005B000u },
    /* windowHi */ { 0x80066000u, 0x80063000u },
    /* codeScanLo, codeScanHi */ 0u, 0u,   // falls back to [recMainLo, recMainHi), correct here:
                                           // no overlays are known resident above the main text
    /* decDctInSync, decDctOutSync */ 0u, 0u,
    // Spyro links stock Sony libcd (the image carries `$Id: bios.c,v 1.86 1997/03/28 ... $`), so the
    // internal primitives are identifiable by the name each one prints:
    //   func_800647A0 CD_sync   func_80064A20 CD_ready   func_80064CEC CD_cw
    //   func_800653B4 CD_init   func_800655A0 CD_datasync
    //
    // cdDataSync <- CD_datasync (0x800655A0): exact name match, and the framework's handler is
    // side-effect-free (report idle, v0=0) — the CD DMA is never started here because reads are
    // native file I/O.
    //
    // cdReadSync stays 0 ON PURPOSE. The obvious candidate is CD_sync (0x800647A0), which the boot log
    // shows spinning — but the framework's cdreadsync handler ZEROES EIGHT BYTES at a1, treating it as
    // CdReadSync(mode, result). CD_sync is an internal bios.c primitive, not that public API, and its
    // signature has not been confirmed. Wiring it on the strength of a similar name would hand the
    // handler an a1 that may not be a result pointer — a guest-memory corruption whose symptom would
    // appear far from here. Confirm the signature first (read the body; check what callers pass in a1).
    /* cdReadSync, cdDataSync      */ 0u, 0x800655A0u,
    // CdInit's low-level controller-ready handshake. func_800653B4 prints "CD_init:addr=" and calls
    // CD_cw (func_80064CEC) at 0x80065510; the boot log shows it looping CdlNop -> CdlReset -> repeat,
    // spinning on a controller-ready bit our no-IRQ runtime never sets. This is the one CD primitive
    // whose role the running system itself demonstrates.
    /* cdInitHandshake             */ 0x800653B4u,
    /* gpuTimeoutArm, gpuTimeoutCheck */ 0u, 0u,
    /* gpuTimeoutDeadlineVar */ 0u,
    /* gpuTimeoutFlagVar     */ 0u,
    /* changeThread          */ 0u,
    /* vsyncTrap             */ 0u,
  },
};

void spyro_install_game_config() { psxport_install_game(&g_spyro_config, spyro_game_hooks()); }
