#include "pause_menu_recipe.h"

namespace spyro::pause_menu {
namespace {

// ── Every address and constant here was read out of SCUS_942.28 ────────────────────────────────
// The string addresses are the guest's own (0x80010B6C etc.); the bytes at each were checked in the
// image, because the decompiler's symbol names for them are wrong: it calls 0x80010B24
// "s_QUIT_GAME" when the image holds "QUIT GAME?", and 0x800755C8 / 0x800755CC "DAT_" when they
// hold "YES" and "NO". A name copied from the decompiler would have been a caption that lies.
constexpr std::uint32_t kStrPaused = 0x800755C0u;        // "PAUSED"
constexpr std::uint32_t kStrYes = 0x800755C8u;           // "YES"
constexpr std::uint32_t kStrNo = 0x800755CCu;            // "NO"
constexpr std::uint32_t kStrCamera = 0x800755D0u;        // "CAMERA"
constexpr std::uint32_t kStrDone = 0x800755D8u;          // "DONE"
constexpr std::uint32_t kStrOff = 0x800755E0u;           // "OFF"
constexpr std::uint32_t kStrMono = 0x800755E4u;          // "MONO"
constexpr std::uint32_t kStrStereo = 0x800755ECu;        // "STEREO"
constexpr std::uint32_t kStrOn = 0x800755F4u;            // "ON"
constexpr std::uint32_t kStrActive = 0x800755F8u;        // "ACTIVE"
constexpr std::uint32_t kStrPassive = 0x80075600u;       // "PASSIVE"
constexpr std::uint32_t kStrOptions = 0x80075608u;       // "OPTIONS"
constexpr std::uint32_t kStrQuit = 0x80075610u;          // "QUIT"
constexpr std::uint32_t kStrQuitGameAsked = 0x80010B24u; // "QUIT GAME?"
constexpr std::uint32_t kStrSoundEffects = 0x80010B30u;  // "SOUND EFFECTS"
constexpr std::uint32_t kStrMusicVolume = 0x80010B40u;   // "MUSIC VOLUME"
constexpr std::uint32_t kStrSpeakerSetup = 0x80010B50u;  // "SPEAKER SETUP"
constexpr std::uint32_t kStrVibration = 0x80010B60u;     // "VIBRATION"
constexpr std::uint32_t kStrContinue = 0x80010B6Cu;      // "CONTINUE"
constexpr std::uint32_t kStrInventory = 0x80010B78u;     // "INVENTORY"
constexpr std::uint32_t kStrExitLevel = 0x80010B84u;     // "EXIT LEVEL"
constexpr std::uint32_t kStrQuitGame = 0x80010B90u;      // "QUIT GAME"

// The two caption lengths the guest passes, per page. The main and confirm pages pass 0x12 and the
// options page 0x10 (0x8001B3BC, 0x8001A9B0, 0x8001AB08).
constexpr std::int32_t kPageCaptionLength = 0x12;
constexpr std::int32_t kOptionsCaptionLength = 0x10;
// 0x8001A968 and every caption call site: the fixed pitch is 0x0B on all three pages.
constexpr std::int32_t kCaptionPitch = 0x0B;

// The caption positions' third component, i.e. the depth the glyph mobys are queued at. The main
// page's items and the confirm page's items are 0x1100 (0x8001B3DC, 0x8001A9D4) and PAUSED is 0xC00
// (0x8001A97C). The options page mixes 0x1400 for the left column (0x8001AB24) with 0x1100 for
// CAMERA and DONE (0x8001AC80).
constexpr std::int32_t kPauseTitleDepth = 0x0C00;
constexpr std::int32_t kPageItemDepth = 0x1100;
constexpr std::int32_t kOptionsLeftDepth = 0x1400;

// The panel's top edge, 0x43, on every page (0x8001A7EC, 0x8001A7F0).
constexpr std::int32_t kPanelTop = 0x43;
// The rule under the title: 0x8001844C(0xE0, 0x61, 0x120, 0x61).
constexpr Segment kTitleRule{0xE0, 0x61, 0x120, 0x61, 0, 0};

// 0x8001844C's endpoint inputs are biased by -0x100 and -0x78 (0x80018468, 0x8001846C), which is
// the 512x240 frame's own centre. The menu is authored in that 512-wide space: the frame's OFX is
// 256, and the box is centred on 0x100.
constexpr std::int32_t kCentreX = 0x100;
constexpr std::int32_t kCentreY = 0x78;

// 0x8001844C: `subu $s1,$zero(0x80),$v0` / `addiu $v1,$s1,0x60` / `sb $v1,4` / `sb $v1,5` / `sb
// $s1,6` — 0x800184C8, 0x800184CC, 0x800184D0, 0x800184D4, 0x800184DC. So the two colour bytes the
// guest computes from one endpoint are 0xE0-shade and 0x80-shade.
constexpr std::uint8_t kBorderHighByte = 0x60; // 0x80 (the complement base) + 0x60
constexpr std::uint8_t kBorderBase = 0x80;

// 0x80017908: `subu $t0,$zero(0x80),$v0` is the complement, and the result is folded into 0..0x80.
std::uint8_t foldShade(std::int32_t directionIndex, std::uint32_t phase) {
  std::uint32_t t =
      static_cast<std::uint32_t>(directionIndex - static_cast<std::int32_t>(phase)) & 0xFFu;
  if (t >= 0x80u) {
    t = 0x100u - t;
  }
  return static_cast<std::uint8_t>(t);
}

// 0x800169AC: a screen offset from the frame centre becomes a palette index. Its min/max ratio
// indexes s_8006d82c, its signs pick a quadrant base, and the sign of the last comparison negates
// the ramp term.
std::int32_t directionIndex(std::int32_t dx, std::int32_t dy, std::span<const std::uint8_t> ramp) {
  const std::int32_t ax = dx < 0 ? -dx : dx;
  const std::int32_t ay = dy < 0 ? -dy : dy;
  std::int32_t lo = ax;
  std::int32_t hi = ay;
  if (ax > ay) {
    lo = ay;
    hi = ax;
  }
  if (hi == 0) {
    hi = 1;
  }
  bool negate = false;
  std::int32_t base = 0;
  if (dx < 0) {
    if (dy < 0) {
      base = (dy - dx) < 0 ? 0xC0 : 0x80;
    } else {
      negate = true;
      base = ((-dy) - dx) < 0 ? 0x40 : 0x80;
    }
  } else if (dy < 0) {
    negate = true;
    base = (dx + dy) < 0 ? 0xC0 : 0x100;
  } else {
    base = (dx - dy) < 0 ? 0x40 : 0x00;
  }
  if (ramp.empty()) {
    return base;
  }
  // The table index is (lo << 6) / hi + 0xDC, and the value is read as a SIGNED byte.
  const std::size_t offset = static_cast<std::size_t>((lo << 6) / hi);
  if (offset >= ramp.size()) {
    return base;
  }
  const std::int32_t value = static_cast<std::int8_t>(ramp[offset]);
  return base + (negate ? -value : value);
}

void addBorder(Recipe &recipe, const State &state, Segment segment) {
  segment.shade0 =
      foldShade(directionIndex(segment.x0 - kCentreX, segment.y0 - kCentreY, state.directionRamp),
                state.lightingPhase);
  segment.shade1 =
      foldShade(directionIndex(segment.x1 - kCentreX, segment.y1 - kCentreY, state.directionRamp),
                state.lightingPhase);
  recipe.border.push_back(segment);
}

Caption caption(
    std::uint32_t text, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t maxLength) {
  return Caption{text, x, y, z, maxLength, true};
}

// The `%10` the guest computes for the main page's fourth item. 0x8001B524-0x8001B54C is
// `mult $a0,0x6667` / `mfhi` / `sra 2` / `subu` / `sll 2` / `addu` / `sll 1` / `beq $a0,$v0` — a
// signed division by ten and a compare against the truncated product.
bool everyTenthTick(std::uint32_t value) {
  const std::int32_t signedValue = static_cast<std::int32_t>(value);
  return signedValue == (signedValue / 10) * 10;
}

} // namespace

Rgb borderColour(std::uint8_t shade) {
  // The three colour bytes the guest stores, in BGR555 packing order: low byte carries red.
  const std::uint8_t low = static_cast<std::uint8_t>(kBorderBase + kBorderHighByte - shade);
  const std::uint8_t mid = low;
  const std::uint8_t high = static_cast<std::uint8_t>(kBorderBase - shade);
  const std::uint32_t word = static_cast<std::uint32_t>(low) |
                             (static_cast<std::uint32_t>(mid) << 8) |
                             (static_cast<std::uint32_t>(high) << 16);
  const std::uint32_t red5 = word & 0x1Fu;
  const std::uint32_t green5 = (word >> 5) & 0x1Fu;
  const std::uint32_t blue5 = (word >> 10) & 0x1Fu;
  // The PSX expands 5-bit channels as (v << 3) | (v >> 2), which is what the rasteriser receives.
  const auto expand = [](std::uint32_t v) {
    return static_cast<std::uint8_t>((v << 3) | (v >> 2));
  };
  return Rgb{expand(red5), expand(green5), expand(blue5)};
}

std::optional<std::uint8_t> panelColourByte(std::uint32_t instructionWord) {
  if (((instructionWord >> 26) & 0x3Fu) != kAddImmediateOpcode) {
    return std::nullopt;
  }
  if (((instructionWord >> 21) & 0x1Fu) != 0u ||
      ((instructionWord >> 16) & 0x1Fu) != kColourRegister) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(instructionWord & 0xFFu);
}

Recipe derive(const State &state) {
  Recipe recipe;
  recipe.page = state.page;
  // The gate is the guest's own: [0x800758B8] == 0 draws the world with no panel at all. On the
  // menu's first frame that is the whole picture, and it is the frame the operator's run aborted
  // on.
  recipe.gui = state.frameCounter != 0u;
  if (!recipe.gui) {
    return recipe;
  }

  // ── The panel and its box, 0x8001A7C4-0x8001A950 ──────────────────────────────────────────────
  // The page test is `bne [0x800757C8],1,0x8001A8F4` (0x8001A894): the options page is the narrow
  // branch, and both other pages take the 0x8C..0x174 box with `local_b0` forced to 1
  // (0x8001A82C-0x8001A834, and 0x8001A914-0x8001A920 which computes local_b0*0x12 + 0x9E).
  const bool options = state.page == Page::Options;
  const std::int32_t panelX0 = options ? 0x54 : 0x8C;
  const std::int32_t panelX1 = options ? 0x1AC : 0x174;
  const std::int32_t panelY1 = options ? (state.vibrationAvailable ? 0xC6 : 0xB6) : 0xB0;
  recipe.panelX0 = panelX0;
  recipe.panelY0 = kPanelTop;
  recipe.panelX1 = panelX1;
  recipe.panelY1 = panelY1;

  addBorder(recipe, state, kTitleRule);
  addBorder(recipe, state, Segment{panelX0, kPanelTop, panelX1, kPanelTop, 0, 0});
  addBorder(recipe, state, Segment{panelX1, kPanelTop, panelX1, panelY1, 0, 0});
  addBorder(recipe, state, Segment{panelX1, panelY1, panelX0, panelY1, 0, 0});
  addBorder(recipe, state, Segment{panelX0, panelY1, panelX0, kPanelTop, 0, 0});

  // "PAUSED" through the FIXED-PITCH builder 0x80017FE4, at 0x8001A980.
  recipe.captions.push_back(Caption{kStrPaused, 0xBA, 0x52, kPauseTitleDepth, 0x1C, false});
  recipe.captionPitch = kCaptionPitch;

  if (state.page == Page::Main) {
    // 0x8001B3AC onward. Four items, and the fourth's caption is a three-way choice: a flight level
    // offers "QUIT" (0x8001B4CC), and otherwise the guest tests its own %10 discriminator
    // (0x8001B54C) between "EXIT LEVEL" and "QUIT GAME".
    recipe.spacing = Spacing{16, 1, 0x1400};
    recipe.captions.push_back(
        caption(kStrContinue, 0xC7, 0x6E, kPageItemDepth, kPageCaptionLength));
    recipe.captions.push_back(caption(kStrOptions, 0xCF, 0x80, kPageItemDepth, kPageCaptionLength));
    recipe.captions.push_back(
        caption(kStrInventory, 0xBF, 0x92, kPageItemDepth, kPageCaptionLength));
    if (state.flightLevel) {
      recipe.captions.push_back(caption(kStrQuit, 0xE7, 0xA4, kPageItemDepth, kPageCaptionLength));
    } else if (everyTenthTick(state.fourthItemPhase)) {
      recipe.captions.push_back(
          caption(kStrQuitGame, 0xBF, 0xA4, kPageItemDepth, kPageCaptionLength));
    } else {
      recipe.captions.push_back(
          caption(kStrExitLevel, 0xB7, 0xA4, kPageItemDepth, kPageCaptionLength));
    }
    return recipe;
  }

  if (state.page == Page::ConfirmQuit) {
    // 0x8001A99C onward. Three captions on the same spacing vector, laid out at 0x77 and 0x94.
    recipe.spacing = Spacing{16, 1, 0x1400};
    recipe.captions.push_back(
        caption(kStrQuitGameAsked, 0xB7, 0x77, kPageItemDepth, kPageCaptionLength));
    recipe.captions.push_back(caption(kStrYes, 0xC0, 0x94, kPageItemDepth, kPageCaptionLength));
    recipe.captions.push_back(caption(kStrNo, 0x128, 0x94, kPageItemDepth, kPageCaptionLength));
    return recipe;
  }

  // OPTIONS, 0x8001AAF0 onward. The left column is 0x6B/0x79/0x6B at y 0x6C/0x7C/0x8C, the
  // VIBRATION row only exists when [0x800756D8] is set, and that same flag shifts CAMERA and DONE
  // down by 0x10 (0x8001AC94, 0x8001ACE4). The right column at x=0x142 carries each setting's
  // current value, and the two volume captions appear only while their row count is 0.
  recipe.spacing = Spacing{15, 1, 0x1600};
  const std::int32_t valueDepth = kOptionsLeftDepth;
  recipe.captions.push_back(
      caption(kStrSoundEffects, 0x6B, 0x6C, valueDepth, kOptionsCaptionLength));
  recipe.captions.push_back(
      caption(kStrMusicVolume, 0x79, 0x7C, valueDepth, kOptionsCaptionLength));
  recipe.captions.push_back(
      caption(kStrSpeakerSetup, 0x6B, 0x8C, valueDepth, kOptionsCaptionLength));
  if (state.vibrationAvailable) {
    recipe.captions.push_back(
        caption(kStrVibration, 0xA3, 0x9C, valueDepth, kOptionsCaptionLength));
  }
  const std::int32_t cameraShift = state.vibrationAvailable ? 0x10 : 0;
  recipe.captions.push_back(
      caption(kStrCamera, 0xD0, cameraShift + 0x9C, kPageItemDepth, kOptionsCaptionLength));
  recipe.captions.push_back(
      caption(kStrDone, 0xEE, cameraShift + 0xAC, kPageItemDepth, kOptionsCaptionLength));
  // The value column, 0x8001AD8C-0x8001AF68.
  if (state.musicRowCount == 0) {
    recipe.captions.push_back(caption(kStrOff, 0x142, 0x6C, valueDepth, kOptionsCaptionLength));
  }
  if (state.soundRowCount == 0) {
    recipe.captions.push_back(caption(kStrOff, 0x142, 0x7C, valueDepth, kOptionsCaptionLength));
  }
  recipe.captions.push_back(caption(state.stereoAvailable ? kStrMono : kStrStereo,
                                    0x142,
                                    0x8C,
                                    valueDepth,
                                    kOptionsCaptionLength));
  if (state.vibrationAvailable) {
    recipe.captions.push_back(caption(
        state.vibrationEnabled ? kStrOn : kStrOff, 0x142, 0x9C, valueDepth, kOptionsCaptionLength));
  }
  recipe.captions.push_back(caption(state.cameraMode == 2u ? kStrActive : kStrPassive,
                                    0x142,
                                    cameraShift + 0x9C,
                                    valueDepth,
                                    kOptionsCaptionLength));
  return recipe;
}

PanelRect placePanel(const Recipe &recipe, DrawOffset offset) {
  return {recipe.panelX0 + offset.x,
          recipe.panelY0 + offset.y,
          recipe.panelX1 + offset.x,
          recipe.panelY1 + offset.y};
}

Segment placeSegment(const Segment &segment, DrawOffset offset) {
  Segment placed = segment;
  placed.x0 += offset.x;
  placed.y0 += offset.y;
  placed.x1 += offset.x;
  placed.y1 += offset.y;
  return placed;
}

} // namespace spyro::pause_menu
