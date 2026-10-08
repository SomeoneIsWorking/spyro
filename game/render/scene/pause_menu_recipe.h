// pause_menu_recipe.h — what guest draw handler 0x8001A40C paints for GS_PauseMenu, as a pure
// derivation over pre-GTE game state.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spyro::pause_menu {

// The three pages 0x8001A40C draws for GS_PauseMenu, read from [0x800757C8]; the dispatch is the
// guest's own two-way branch, 0x8001A994 for page 2, 0x8001AAF0 for page 1, page 0 falling through.
enum class Page : std::uint32_t {
  Main = 0,       // CONTINUE / OPTIONS / INVENTORY / QUIT GAME
  Options = 1,    // SOUND EFFECTS / MUSIC VOLUME / SPEAKER SETUP / VIBRATION / CAMERA / DONE
  ConfirmQuit = 2 // QUIT GAME? / YES / NO
};

// Every field is pre-GTE: this layer runs before any producer has projected anything, and the
// derivation never touches the GTE, the OT, or a captured frame.
//
// The four OPTIONS-page gates are named by what they select in the draw, not by a recovered symbol.
struct State {
  std::uint32_t frameCounter = 0;    // [0x800758B8] — 0 on the menu's first frame: world only
  Page page = Page::Main;            // [0x800757C8]
  std::uint32_t selected = 0;        // [0x80075720]
  bool flightLevel = false;          // [0x80075690] g_IsFlightLevel
  std::uint32_t fourthItemPhase = 0; // [0x8007596C] — the fourth item's own %10 discriminator
  bool vibrationAvailable = false;   // [0x800756D8] — adds the VIBRATION row and shifts CAMERA/DONE
  std::uint32_t soundRowCount = 0;   // [0x80075748] — 0 draws the bare "OFF" caption at y=0x7C
  std::uint32_t musicRowCount = 0;   // [0x80075754] — 0 draws the bare "OFF" caption at y=0x6C
  bool stereoAvailable = false;      // [0x80076240] — false draws "STEREO", true draws "MONO"
  bool vibrationEnabled = false;     // [0x800757A4] — 0 draws "OFF", non-zero draws "ON"
  std::uint32_t cameraMode = 0;      // [0x80075914] — ==2 draws "ACTIVE", else "PASSIVE"
  std::uint32_t lightingPhase = 0;   // [0x800770F4] — the border's palette rotation
  // s_8006d82c[ratio + 0xDC], the ramp 0x800169AC indexes. 0x8006D82C + 0xDC .. + 0xDC + 64.
  std::span<const std::uint8_t> directionRamp;
};

struct Segment {
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t x1 = 0;
  std::int32_t y1 = 0;
  // 0x80017908's byte for each endpoint, 0..0x80. The colour comes from borderColour.
  std::uint8_t shade0 = 0;
  std::uint8_t shade1 = 0;
};

// A caption as the guest passes it: the ADDRESS of its string in the main image, and the position
// vector 0x80017FE4 / 0x800181AC receive by pointer; the bytes are read from the guest's memory.
struct Caption {
  std::uint32_t text = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::int32_t maxLength = 0;
  bool proportional = false; // false = 0x80017FE4 fixed pitch, true = 0x800181AC proportional
};

// The spacing vector the guest passes by pointer to 0x800181AC, one value per page: {15, 1, 0x1600}
// for options, {16, 1, 0x1400} for confirm and main.
struct Spacing {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

struct Recipe {
  // False on the menu's first frame: [0x800758B8] is the menu's animation tick, read as
  // tick * 8 + i indexing COSINE_8 to bob the cursor, so its first zero frame draws world only.
  bool gui = false;
  Page page = Page::Main;
  // The panel quad, in the guest's own 512x240 screen coordinates.
  std::int32_t panelX0 = 0;
  std::int32_t panelY0 = 0;
  std::int32_t panelX1 = 0;
  std::int32_t panelY1 = 0;
  std::vector<Segment> border;
  std::vector<Caption> captions;
  Spacing spacing;
  std::int32_t captionPitch = 0; // 0x0B on every page that has captions
};

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// 0x8001844C's flat border colour: one 24-bit BGR555 word is (0xE0-shade, 0xE0-shade, 0x80-shade).
Rgb borderColour(std::uint8_t shade);

// 0x8001844C's endpoint lighting: each endpoint's shade is 0x80017908 over 0x800169AC's direction
// index from the frame centre. The fairy menu's box edges are the same builder's lines.
Segment litSegment(Segment segment, std::span<const std::uint8_t> ramp, std::uint32_t phase);

// The panel's colour byte is 0x40, not 0xE0: the handler's `addiu $s4,$zero,0xE0` at 0x8001A450 is
// on the world arm, which never reaches the panel's colour stores.
std::optional<std::uint8_t> panelColourByte(std::uint32_t instructionWord);

// The guest address of that definition, read from the resident executable.
constexpr std::uint32_t kPanelColourDefinitionPc = 0x8001A6C8u;
// MIPS32 `addiu` opcode and its expected `rt` ($s4); `rs` must be $zero.
constexpr std::uint32_t kAddImmediateOpcode = 0x09u;
constexpr std::uint32_t kColourRegister = 20u; // $s4
// 0x8001A7D0 stores the GP0 command byte; bit 15 of the assembled word is the semi-transparency
// bit.
constexpr std::uint8_t kPanelStp = 1;

// 0x8001A5E0's GUI, derived from `state`. Pure: no Core, no GPU, no guest write.
Recipe derive(const State &state);

// The GPU drawing offset (GP0 E5). A recipe is in offset-relative coordinates and the hardware adds
// this to every vertex; the two display buffers differ only in it (y 0 or 240).
struct DrawOffset {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

struct PanelRect {
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t x1 = 0;
  std::int32_t y1 = 0;
};

// The panel quad and one border segment in framebuffer coordinates.
PanelRect placePanel(const Recipe &recipe, DrawOffset offset);
Segment placeSegment(const Segment &segment, DrawOffset offset);

} // namespace spyro::pause_menu
