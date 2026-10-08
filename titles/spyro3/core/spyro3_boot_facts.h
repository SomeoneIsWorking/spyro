#pragma once

#include "boot_prefix_frame_driver.h"

namespace spyro3 {

// SCUS_944.67's boot-prefix facts, MEASURED with external/psxport/tools/disasm.py over a RAM image
// built the PS-X EXE way (file[0x800] -> t_addr 0x80010000). The bytes are quoted so each fact can
// be re-derived rather than trusted.
//
// Game main 0x8001200C walks the constructor table at 0x800594EC, enters the retail boot prefix
// 0x8002AB38, and its per-frame loop is the retail pair 0x80055400 (the update) followed by
// 0x8001E638 (the draw, the leaf carrying this title's display waits):
//
//   8001200C  addiu $sp,$sp,-0x18 ; sw $ra,0x10($sp)  game main
//   80012014  jal  0x800594EC                          constructor table walk
//   8001201C  jal  0x8002AB38                          the boot prefix
//   80012024  jal  0x80055400                          the per-frame update, and the loop head
//   8001202C  jal  0x8001E638                          the draw, after the update
//   80012034  j    0x80012024                          the retail loop is these two and nothing
//                                                      else
//
//   8002AB38  addiu $sp,$sp,-0x40 ; sw $ra,0x3c($sp)  boot prefix entry
//   8002AB48  jal  0x8005C684                          first leaf; 0x8005C684 loads the table
//                                                        pointer at 0x8006B350 and does `jalr` on
//                                                        its word 3, so it dispatches through the
//                                                        guest's own handler table
//   8002AB50  jal  0x8002A834                          display bootstrap
//   8002A848  jal  0x8005956C  (a0 = 0)               its VSync(0), the first display wait
//   8002AB58  jal  0x8002A794
//   8002AB60  jal  0x8002A7B4                          CD bootstrap, and inside it
//   8002A7C0    jal 0x8005DB1C                           CdInit
//   8002A7D0    jal 0x8005E0BC  (a0 = 0x0E)             CdCommand(CdlReadS)
//   8002A7E0    jal 0x8005DB08  (a0 = 0x80050504)       CdReadyCallback, this title's own
//                                                        per-sector reader
//   8002AB68  jal  0x8004F8EC
//   8002AB70  jal  0x8002A99C                          geometry init, which calls SetGeomOffset
//                                                        at 0x8002A9B0 with (0x100, 0x78)
//   8002ABA4  jal  0x80050578
//   8002ACB4  jal  0x80074DEC  (a0 = 1)                a loaded module's entry, OUTSIDE the
//                                                        resident text
//
// The libcd and libgpu leaves are named in the runtime's measured plan, where they are owned.
//
// Bounds: the field bound stands, the step bound is this title's own. MEASURED 2026-10-01: once the
// loaded modules are published as images (game/core/stock_read_publication.*) and the framework
// paces each stock CdRead by the drive's seek and sector time (psxport issue on paced stock
// completions), the boot prefix returns after 887 fields in 669 steps; with instantaneous reads it
// was 546 fields in 336 steps, which is why the shared 480-step bound no longer holds. 1024 steps
// is the measured 669 plus half again: a guest that polls without ever asking for a field still
// ends the run long before the turn budget, and the 887 fields stay under the shared 1024 field
// bound. The first field bound was 64, a guess made while the boot stopped at the module load, and
// it ended a healthy boot mid-fade.
inline constexpr spyro::BootPrefixFacts kBootPrefixFacts{
    .titleName = "Spyro 3",
    .callName = "spyro3-step",
    .bootPrefix = 0x8002AB38u,
    .frameUpdate = 0x80055400u,
    .frameDraw = 0x8001E638u,
    .field =
        {
            .titleName = "Spyro 3",
            // The guest word the host advances once per delivered field. Zero says this title has
            // no measured field counter of its own yet, so the host owns no guest word and only
            // the framework's presentation fence counts fields; it is stated rather than invented.
            .fieldCounter = 0,
            // No guest IRQ root is claimed: no vblank root handler slot has been measured for
            // SCUS_944.67, so the framework's own interrupt path owns the callback.
            .rootHandlerSlot = 0,
            .handlerStackTop = 0x8000E000u,
            .handlerStackBytes = 8192u,
            // The shared field cadence this lineage's titles declare. It is NOT a Spyro 3
            // measurement: no per-frame field count has been read out of SCUS_944.67 yet, and the
            // driver never arms the host field clock, so nothing in the boot depends on the
            // number. It is stated here rather than left to look measured.
            .fieldsPerLogicFrame = 2,
        },
    .bootStepLimit = 1024,
};

} // namespace spyro3
