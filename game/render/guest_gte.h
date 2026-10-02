// guest_gte.h — the GTE registers and commands the native render overrides of this engine family
// issue, named once for every owner under game/render/.
//
// The register numbers are the COP2 numbers `mtc2`/`mfc2` (data) and `ctc2`/`cfc2` (control) name.
// The commands are the complete COP2 instruction words SCUS_944.25 holds, because `gte_op` decodes
// the same word the image executes: the shift, saturation and matrix fields are part of the word.
//
// the hand-written render routines run out of general registers and keep their own values in
// GTE registers whose hardware meaning they never use (the light matrix, the far colour, TR). The
// names here are the hardware's; each owner names what it keeps in them where it does so.
#pragma once

#include <cstdint>

namespace spyro::guest_gte {

// Data registers.
inline constexpr std::uint32_t kVxy0 = 0;
inline constexpr std::uint32_t kVz0 = 1;
inline constexpr std::uint32_t kVxy1 = 2;
inline constexpr std::uint32_t kVz1 = 3;
inline constexpr std::uint32_t kRgbc = 6;
inline constexpr std::uint32_t kIr0 = 8;
inline constexpr std::uint32_t kIr1 = 9;
inline constexpr std::uint32_t kIr2 = 10;
inline constexpr std::uint32_t kIr3 = 11;
inline constexpr std::uint32_t kSxy0 = 12;
inline constexpr std::uint32_t kSxy1 = 13;
inline constexpr std::uint32_t kSxy2 = 14;
inline constexpr std::uint32_t kSz3 = 19;
inline constexpr std::uint32_t kRgb2 = 22;
inline constexpr std::uint32_t kMac0 = 24;
inline constexpr std::uint32_t kMac1 = 25;
inline constexpr std::uint32_t kMac2 = 26;
inline constexpr std::uint32_t kMac3 = 27;

// Control registers.
inline constexpr std::uint32_t kRotation0 = 0; // RT11RT12 .. RT33 are CR0..CR4
inline constexpr std::uint32_t kRotationWords = 5;
inline constexpr std::uint32_t kTranslationX = 5;
inline constexpr std::uint32_t kTranslationY = 6;
inline constexpr std::uint32_t kTranslationZ = 7;
inline constexpr std::uint32_t kLight0 = 8; // L11L12 .. L33 are CR8..CR12
inline constexpr std::uint32_t kLight1 = 9;
inline constexpr std::uint32_t kLight2 = 10;
inline constexpr std::uint32_t kLight3 = 11;
inline constexpr std::uint32_t kLight4 = 12;
inline constexpr std::uint32_t kFarRed = 21;
inline constexpr std::uint32_t kFarGreen = 22;
inline constexpr std::uint32_t kFarBlue = 23;
inline constexpr std::uint32_t kFlag = 31;

// Commands, as the full COP2 instruction words the image holds.
inline constexpr std::uint32_t kRotateIr = 0x4A49E012u;       // MVMVA sf, RT * IR, no offset
inline constexpr std::uint32_t kRotateV0 = 0x4A486012u;       // MVMVA sf, RT * V0, no offset
inline constexpr std::uint32_t kRotateIrPlusTr = 0x4A498012u; // MVMVA sf, RT * IR + TR
inline constexpr std::uint32_t kScaleIr = 0x4B90003Du;        // GPF sf, MAC = IR0 * IR
inline constexpr std::uint32_t kProject = 0x4A180001u;        // RTPS
inline constexpr std::uint32_t kWinding = 0x4B400006u;        // NCLIP
inline constexpr std::uint32_t kFadeColour = 0x4A780010u;     // DPCS: RGBC toward FC by IR0
inline constexpr std::uint32_t kFadeVector = 0x4A980011u;     // INTPL: IR toward FC by IR0

} // namespace spyro::guest_gte
