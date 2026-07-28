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
  /* irqEventClasses  */ { 0u, 0u, 0u },
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
  /* cdCommand         */ 0u,
  /* cdSync            */ 0u,
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
};

void spyro_install_game_config() { psxport_install_game(&g_spyro_config, spyro_game_hooks()); }
