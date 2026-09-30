#pragma once

#include "boot_prefix_frame_driver.h"

namespace spyro2 {

// SCUS_944.25's boot-prefix facts, MEASURED with external/psxport/tools/disasm.py over a RAM image
// built the PS-X EXE way (file[0x800] -> t_addr 0x80010000). The bytes are quoted so each fact can
// be re-derived rather than trusted.
//
// Game main 0x80011ADC calls the boot prefix 0x80011E9C, which brings up the display through libetc
// VSync 0x80058EDC and then runs the loader chain:
//
//   80011E9C  addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)   boot prefix entry
//   80011EA4  jal  0x800548A4                          first leaf (display/libc init)
//   80011EAC  jal  0x80011BBC                          display bootstrap: VSync(0) at
//   80011BD0  jal  0x80058EDC                          libetc 0x80058EDC, then three fields
//   80011EB4  jal  0x80011B1C                          leaf the retired bootstrap stopped at
//   80011EBC  jal  0x80011B3C                          CD bootstrap
//   80011EC4  jal  0x80012B84                          music init
//   80011ECC  jal  0x80011D24                          geometry init
//   80011EF8  jal  0x80013810                          module load
//   80011F0C  jal  0x80077374  (a0=1)                  the loaded module's entry
//
// and the retail per-frame loop is the update/draw pair and nothing else:
//
//   80011AEC  jal  0x8001B140                          game main's per-frame update
//   80011AF4  jal  0x800156FC                          game main's draw, after the update, which
//   80011AFC  jal  0x800156FC ; 80011B04  j 0x80011AF4 owns the frame's display wait
//
// Bounds: the shared defaults stand. MEASURED 2026-10-01: once the loaded module is published as an
// image (game/core/stock_read_publication.*) the boot prefix returns after 133 fields in 43 steps,
// inside both. The first field bound was 64, a guess made while the boot stopped at the module
// load, and it ended a healthy boot mid-fade.
//
// Measured over 30 boot fields before that: every field is a guest display wait, and the product
// step count (27) is below the field count (30), which is what a starved host clock would not
// produce.
inline constexpr spyro::BootPrefixFacts kBootPrefixFacts{
    .titleName = "Spyro 2",
    .callName = "spyro2-step",
    .bootPrefix = 0x80011E9Cu,
    .frameUpdate = 0x8001B140u,
    .frameDraw = 0x800156FCu,
    .field =
        {
            .titleName = "Spyro 2",
            // The guest word the host advances once per delivered field. Zero says this title has
            // no measured field counter of its own yet, so the host owns no guest word and only
            // the framework's presentation fence counts fields; it is stated rather than invented.
            .fieldCounter = 0,
            // No guest IRQ root is claimed: SCUS_944.25 installs the BIOS HookEntryInt
            // continuation as its libetc vblank callback (0x80054A78 stores B(0x19) and
            // 0x80054A84 registers it through VSyncCallback 0x8005AC34), which the framework's own
            // interrupt path resumes.
            .rootHandlerSlot = 0,
            .handlerStackTop = 0x8000E000u,
            .handlerStackBytes = 8192u,
            .fieldsPerLogicFrame = 2,
        },
};

} // namespace spyro2
