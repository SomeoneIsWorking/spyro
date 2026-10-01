// pause_menu_recipe.h — what guest draw handler 0x8001A40C paints for GS_PauseMenu, as a pure
// derivation over pre-GTE game state.
//
// THE HANDLER, FROM THE IMAGE. SCUS_942.28 decompiles 0x8001A40C (8,840 bytes, the largest arm of
// the render driver 0x8001ED5C) to two mutually exclusive paths, selected by one global read at
// 0x8001A410:
//
//   [0x800758B8] == 0  -> 0x8001A444, "the world, and nothing else":
//       0x800521C0 moby list build, 0x80019698 actor pass, 0x800573C8 particles, 0x80050BD0
//       cyclorama, 0x8002B9CC environment, then the display tail (0x8005F764 DrawSync, 0x8005DBC4
//       VSync, 0x80060030 PutDispEnv, 0x8005FDD8 PutDrawEnv).
//
//   [0x800758B8] != 0  -> 0x8001A5E0, the menu:
//       0x8005FDD8 PutDrawEnv, then four tiled quads (from 0x8001A664), the panel, five 0x8001844C
//       border lines, and the page's captions through 0x80017FE4 / 0x800181AC.
//
// [0x800758B8] IS THE MENU'S OWN ANIMATION TICK, NOT A "READY" FLAG, and that is what the abort
// frame measures: the handler reads it as `_DAT_800758b8 * 8 + iVar13` indexing COSINE_8 to bob the
// selection cursor (0x8001AD58-0x8001AD64), and the refusal snapshot at the very first GS_PauseMenu
// frame reads it 0. So the zero path is the menu's FIRST FRAME ONLY: the world is on screen, the
// panel is not, and from the second frame the menu is drawn over that same frozen world. This owner
// reproduces the gate rather than smoothing it away, because the first frame is the frame the
// operator's run aborted on.
//
// WHY THE FOUR TILED QUADS ARE NOT A LAYER HERE. They are 4-vertex quads 128 wide and 224 tall at
// x = 0,128,256,384, y = 8..231, that 0x8001A5E0 draws from the texture rect (512,0) 256x225. The
// zero path fills that rect after a world frame: 0x8005FA8C StoreImage parks the rect's textures in
// RAM below the HUD OT, then per tile StoreImage of the framebuffer, 0x80017E98 (an in-place RGB555
// -> 3-3-2 conversion of exactly 0x7000 = 128*224 pixels) and 0x8005FA28 LoadImage over the rect.
// The menu's exit (0x8002C534 / 0x8002C7BC) loads the parked textures back, so the PARK is a VRAM
// lifecycle step the scene owner keeps (pause_menu_scene); only the copy is dropped. A per-tile
// pixel-count constant, a colour-space conversion, and a VRAM round trip through a
// framebuffer-sized buffer are a FROZEN COPY OF THE JUST-DRAWN WORLD, not menu art: the game
// captures the field once and re-blits it while the menu is up rather than redrawing an unchanging
// scene. This port draws the world through its own producers on every menu frame instead, which is
// the same picture and is neither a captured framebuffer nor wrong under widescreen (the port's
// frame is wider than the 512x240 the guest captured). Everything the menu ADDS is owned here.
//
// THE PANEL IS THE VISIBLE PART, and it is not subtle: 0x8001A7C4-0x8001A84C builds a GP0(0x2A)
// untextured quad whose three colour bytes are whatever `$s4` holds at 0x8001A7D8-0x8001A7E0, i.e.
// BGR555 with bit 15 set — a dark SEMI-TRANSPARENT wash. The five 0x8001844C calls draw that box's
// outline plus the rule under the title, each endpoint lit through 0x800169AC / 0x80017908 off a
// byte table in the main image. The captions are the guest's own two HUD text builders, which
// game/render/hud_text_builder already owns.
//
// The colour byte is 0x40 (decomp draw.c:986 `setRGB0(f4, 64, 64, 64)`), not 0xE0: the handler's
// `addiu $s4,$zero,0xE0` at 0x8001A450 is on the world arm, which never reaches the panel.
// tools/probe_pause_panel_colour.py proves exactly one of the 29 `$s4` definitions reaches the
// panel's colour stores. See docs/issues/0144.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spyro::pause_menu {

// The three pages 0x8001A40C draws for GS_PauseMenu, read from [0x800757C8]. The dispatch is the
// guest's own two-way branch: 0x8001A994 leaves for page 2, 0x8001AAF0 for page 1, and page 0 falls
// through to 0x8001B3AC.
enum class Page : std::uint32_t {
  Main = 0,       // CONTINUE / OPTIONS / INVENTORY / QUIT GAME
  Options = 1,    // SOUND EFFECTS / MUSIC VOLUME / SPEAKER SETUP / VIBRATION / CAMERA / DONE
  ConfirmQuit = 2 // QUIT GAME? / YES / NO
};

// The game state this derivation reads. Every field is pre-GTE: this layer runs before any producer
// has projected anything, and the derivation never touches the GTE, the OT, or a captured frame.
//
// The four OPTIONS-page gates are NAMED BY WHAT THEY SELECT IN THE DRAW, not by a recovered symbol:
// nothing in this repository has a name for 0x80075748, 0x80075754, 0x80075914 or 0x80076240, and
// inventing a role for them would read as knowledge. What each one chooses is stated at its field.
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
// vector 0x80017FE4 / 0x800181AC receive by pointer. The string bytes are the guest's own, read
// through the title's memory, so a caption cannot drift from the text the binary holds.
struct Caption {
  std::uint32_t text = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::int32_t maxLength = 0;
  bool proportional = false; // false = 0x80017FE4 fixed pitch, true = 0x800181AC proportional
};

// The spacing vector the guest passes by pointer to 0x800181AC. One value per page: 0x8001AB0C sets
// {15, 1, 0x1600} for the options page, and 0x8001A9B4 sets {16, 1, 0x1400} for the confirm page
// and the main page.
struct Spacing {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

struct Recipe {
  // False on the menu's first frame, where the guest draws the world and no panel. True from the
  // second frame on.
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

// 0x8001844C's flat border colour, from its own instruction words: `subu $s1,$zero(0x80),$v0` then
// `addiu $v1,$s1,0x60` for colour bytes 4 and 5, and `$s1` itself for byte 6
// (0x800184C8-0x800184D4, 0x800184F0-0x80018504). So one 24-bit BGR555 word is (0xE0-shade,
// 0xE0-shade, 0x80-shade), and the 5-bit fields expand the way the PSX expands them.
Rgb borderColour(std::uint8_t shade);

// 0x8001844C's endpoint lighting, which every menu line the guest draws goes through: each
// endpoint's shade is 0x80017908 over 0x800169AC's direction index from the frame centre. Shared by
// the fairy menu, whose box edges are the same builder's lines.
Segment litSegment(Segment segment, std::span<const std::uint8_t> ramp, std::uint32_t phase);

// The panel's colour byte: the guest stores `$s4` with `sb` at 0x8001A7D8-0x8001A7E0, and the one
// reaching definition is `addiu $s4,$zero,imm` at kPanelColourDefinitionPc. Returns the immediate's
// low byte, or nothing when the word is not that exact instruction.
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

// The GPU drawing offset (GP0 E5). A recipe is in the guest's offset-relative coordinates and the
// hardware adds this to every vertex; the two display buffers differ only in it (y 0 or 240), so a
// quad placed without it lands outside the draw area on every other frame.
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
