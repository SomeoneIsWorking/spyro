// spyro2_sector_gte.h — the GTE registers and commands Spyro 2's terrain sector walk (0x80043858)
// uses, named once for its two owners (spyro2_sector_visibility, spyro2_sector_rotation).
//
// The walk is hand-written assembly that runs out of general registers, so it parks state in GTE
// registers whose hardware meaning it never uses: VXY1/VZ1 carry two values to the sector drawer
// that runs next, the light-matrix words CR8..CR11 hold list cursors, and TRX..TRZ hold the
// current sector's view-space centre. Each is named for what this routine keeps in it.
#pragma once

#include <cstdint>

namespace spyro2::sector_gte {

// Data registers (mtc2/mfc2 numbers).
inline constexpr std::uint32_t kVxy0 = 0;
inline constexpr std::uint32_t kVz0 = 1;
inline constexpr std::uint32_t kVxy1 = 2;
inline constexpr std::uint32_t kVz1 = 3;
inline constexpr std::uint32_t kIr0 = 8;
inline constexpr std::uint32_t kIr1 = 9;
inline constexpr std::uint32_t kIr2 = 10;
inline constexpr std::uint32_t kIr3 = 11;
inline constexpr std::uint32_t kMac1 = 25;
inline constexpr std::uint32_t kMac2 = 26;
inline constexpr std::uint32_t kMac3 = 27;

// Control registers (ctc2/cfc2 numbers).
inline constexpr std::uint32_t kRotation0 = 0; // RT11RT12 .. RT33 are CR0..CR4
inline constexpr std::uint32_t kRotationWords = 5;
inline constexpr std::uint32_t kTranslationX = 5;
inline constexpr std::uint32_t kTranslationY = 6;
inline constexpr std::uint32_t kTranslationZ = 7;
inline constexpr std::uint32_t kDeferredCursor = 8; // L11L12: next free deferred-sector slot
inline constexpr std::uint32_t kDeferredEnd = 9;    // L13L21: end of the deferred-sector buffer
inline constexpr std::uint32_t kParkedCursor = 10;  // L22L23: the sky-list cursor, while LO is busy
inline constexpr std::uint32_t kParkedListEnd = 11; // L31L32: the render-list end, while HI is busy

// Commands, as the full COP2 instruction words the image holds.
inline constexpr std::uint32_t kRotateIr = 0x4A49E012u;       // MVMVA sf, RT * IR, no offset
inline constexpr std::uint32_t kRotateV0 = 0x4A486012u;       // MVMVA sf, RT * V0, no offset
inline constexpr std::uint32_t kRotateIrPlusTr = 0x4A498012u; // MVMVA sf, RT * IR + TR
inline constexpr std::uint32_t kScaleIr = 0x4B90003Du;        // GPF, MAC = IR0 * IR

} // namespace spyro2::sector_gte
