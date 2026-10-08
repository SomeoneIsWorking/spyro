#include "pause_menu_scene.h"

#include "core.h"
#include "field_actor_composition.h"
#include "game.h"
#include "gpu_native_internal.h"
#include "guest_call.h"
#include "guest_globals.h"
#include "hud_text_builder.h"
#include "menu_lighting.h"
#include "menu_panel_submit.h"
#include "menu_world_pass.h"
#include "pause_menu_recipe.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "ui_anchor.h"

#include <array>
#include <lucent/log.h>
#include <optional>
#include <string>
#include <vector>

namespace spyro::pause_menu_scene {
namespace {

using spyro::pause_menu::Caption;
using spyro::pause_menu::Page;
using spyro::pause_menu::Recipe;
using spyro::pause_menu::Segment;
using spyro::pause_menu::State;

// The globals 0x8001A40C itself reads and writes.

// [0x800758B8] is the menu's own frame counter, not a ready flag: the guest's epilogue at
// 0x8001C65C increments it, so only the first frame takes the world path.
constexpr std::uint32_t kMenuFrameCounter = 0x800758B8u;
constexpr std::uint32_t kMenuPage = 0x800757C8u; // the three-way page branch
constexpr std::uint32_t kMenuSelected = 0x80075720u;
constexpr std::uint32_t kIsFlightLevel = 0x80075690u;
constexpr std::uint32_t kFourthItemPhase = 0x8007596Cu;
constexpr std::uint32_t kVibrationAvailable = 0x800756D8u;
constexpr std::uint32_t kSoundRowCount = 0x80075748u;
constexpr std::uint32_t kMusicRowCount = 0x80075754u;
constexpr std::uint32_t kStereoAvailable = 0x80076240u;
constexpr std::uint32_t kVibrationEnabled = 0x800757A4u;
constexpr std::uint32_t kCameraMode = 0x80075914u;
// 0x80018880, the guest's "append the HUD mobys I just built to the shaded queue" leaf, shared with
// the level-transition tally so HUD text has one route.
constexpr std::uint32_t kCopyHudMobys = 0x80018880u;
// g_SonyImage.m_ShadedMobys' world queue head. Zeroed before the copy so the range the copy appends
// is a complete list and not the tail of a previous screen's.
constexpr std::uint32_t kShadedMobyQueue = 0x8006FCF4u + 0x2400u;

// The panel and border are the handler's own prims, so they are keyed by the handler's address.
constexpr std::uint32_t kProducerKey = 0x8001A40Cu;

// What the guest's shared builders 0x80017FE4 / 0x800181AC write, hence also the tally's value.
constexpr std::uint8_t kCaptionShade = 2;

// The menu's own pools, from 0x8001A604-0x8001A650.
constexpr std::uint32_t kMenuPrimPoolBase = 0x800785E8u;
constexpr std::uint32_t kMenuMobyPoolBase = 0x800785F0u;
constexpr std::uint32_t kMenuPrimPoolCursor = 0x800757B0u;
constexpr std::uint32_t kMenuPrimPoolEnd = 0x80075780u;
constexpr std::uint32_t kMenuMobyArenaCursor = 0x80075710u;
constexpr std::uint32_t kMenuMobyArenaEnd = 0x800756FCu;
constexpr std::uint32_t kMenuOverlayPrimCursor = 0x800758B0u;
constexpr std::uint32_t kMenuPrimPoolBytes = 0x1C000u;
constexpr std::uint32_t kMenuMobyBaseAdjust = 0xFFFE3E00u;

// The rect the guest lends to its frozen backdrop, and the RAM it is parked in while lent. Leaving
// the menu restores that save unconditionally, so the save must happen on the world path too.
constexpr std::uint32_t kBackdropRectX = 512u;
constexpr std::uint32_t kBackdropRectY = 0u;
constexpr std::uint32_t kBackdropRectW = 256u;
constexpr std::uint32_t kBackdropRectH = 225u;
constexpr std::uint32_t kGp0VramToCpu = 0xC0000000u;
// Record sizes, from the cursor steps the guest's own code performs: `DAT_800757b0 = puVar1 + 0x17`
// after a prim at puVar1[0x11] is 6 words, and 0x8001844C's `addiu $s0,$s0,0x14` is 5.
constexpr std::uint32_t kPanelPrimBytes = 6u * 4u;
constexpr std::uint32_t kLinePrimBytes = 5u * 4u;

// The one address both the menu arena and the borrowed-texture save are relative to.
std::uint32_t hudOtSaveBase(Core *core) {
  return core->mem_r32(kMenuMobyPoolBase) + kMenuMobyBaseAdjust;
}

// The guest's StoreImage of the borrowed rect: GP0(0xC0) plus a VRAM->CPU DMA2 into the save
// buffer. Only the save is reproduced; the live world is drawn instead of the frozen copy.
void saveBorrowedTextures(Core *core) {
  gpu_gp0(core, kGp0VramToCpu);
  gpu_gp0(core, (kBackdropRectY << 16) | kBackdropRectX);
  gpu_gp0(core, (kBackdropRectH << 16) | kBackdropRectW);
  gpu_dma2_block(
      core, hudOtSaveBase(core), static_cast<int>((kBackdropRectW * kBackdropRectH + 1u) / 2u), 0);
}

// `lighting` is borrowed by the returned State's ramp span, so the caller keeps it alive for as
// long as the State is used.
State readState(Core *core, const menu_lighting::Lighting &lighting) {
  State state;
  state.frameCounter = core->mem_r32(kMenuFrameCounter);
  state.page = static_cast<Page>(core->mem_r32(kMenuPage));
  state.selected = core->mem_r32(kMenuSelected);
  state.flightLevel = core->mem_r32(kIsFlightLevel) != 0u;
  state.fourthItemPhase = core->mem_r32(kFourthItemPhase);
  state.vibrationAvailable = core->mem_r32(kVibrationAvailable) != 0u;
  state.soundRowCount = core->mem_r32(kSoundRowCount);
  state.musicRowCount = core->mem_r32(kMusicRowCount);
  state.stereoAvailable = core->mem_r32(kStereoAvailable) != 0u;
  state.vibrationEnabled = core->mem_r32(kVibrationEnabled) != 0u;
  state.cameraMode = core->mem_r32(kCameraMode);
  state.lightingPhase = lighting.phase;
  state.directionRamp = lighting.ramp;
  return state;
}

std::string readString(Core *core, std::uint32_t address) {
  std::string out;
  for (std::uint32_t i = 0; i < 64u; ++i) {
    const std::uint8_t ch = core->mem_r8(address + i);
    if (ch == 0u) {
      break;
    }
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

// 0x2A is an untextured 4-vertex quad whose bit 15 is the semi-transparency bit, so the panel is a
// dark translucent wash. Its colour byte is the guest's `addiu $s4,$zero,imm` immediate.
std::optional<std::uint8_t> readPanelColourByte(Core *core) {
  return spyro::pause_menu::panelColourByte(
      core->mem_r32(spyro::pause_menu::kPanelColourDefinitionPc));
}

// Both pages' boxes are symmetric about 0x100, half of the guest's own 512-wide screen, and the
// five border segments are the edges of that same box, so they take the panel's class.
constexpr spyro::ui_anchor::Anchor kPanelAnchor = spyro::ui_anchor::Anchor::Centred;

// The border's five lines are the edges of the panel's box, so they take the panel's shift: a
// vertical edge has zero width and is not a box the anchoring owner could place on its own.
std::int32_t panelShift(Core *core, const Recipe &recipe) {
  const GpuState &gpu = core->game->gpu;
  const auto authored = spyro::pause_menu::placePanel(recipe, {gpu.s_off_x, gpu.s_off_y});
  return spyro::ui_anchor::placeAndReport({"pause-panel"},
                                          kPanelAnchor,
                                          authored.x0,
                                          authored.x1 - authored.x0,
                                          spyro::ui_anchor::frame(core))
      .offset;
}

// The panel's three colour bytes are the one `$s4` byte stored three times, read here as the 5-bit
// fields of a BGR555 word and expanded the way the PSX expands them.
menu_panel::Colour expandPanelColour(std::uint8_t colourByte) {
  const std::uint32_t word = static_cast<std::uint32_t>(colourByte) |
                             (static_cast<std::uint32_t>(colourByte) << 8) |
                             (static_cast<std::uint32_t>(colourByte) << 16);
  const auto expand = [](std::uint32_t v) {
    return static_cast<unsigned char>((v << 3) | (v >> 2));
  };
  return {expand(word & 0x1Fu), expand((word >> 5) & 0x1Fu), expand((word >> 10) & 0x1Fu)};
}

// Through the two owners that already hold the guest's text: the layout half in hud_text_builder
// (0x80017FE4 / 0x800181AC) and the shaded-moby pass 0x80022A2C.
bool submitText(Core *core, const Recipe &recipe) {
  std::size_t glyphs = 0;
  for (const Caption &caption : recipe.captions) {
    const std::string text = readString(core, caption.text);
    if (text.empty()) {
      return false;
    }
    glyphs += text.size();
  }
  if (!spyro::hud_text::fits(core, glyphs)) {
    return false;
  }
  for (const Caption &caption : recipe.captions) {
    const std::string text = readString(core, caption.text);
    const spyro::hud_text::Point3 position{caption.x, caption.y, caption.z};
    const spyro::hud_text::Point3 spacing{recipe.spacing.x, recipe.spacing.y, recipe.spacing.z};
    const auto layout =
        caption.proportional
            ? spyro::hud_text::layoutCaption(text, position, spacing, caption.maxLength)
            : spyro::hud_text::layoutCounter(text, position, recipe.captionPitch);
    if (spyro::hud_text::append(core, layout, kCaptionShade).size() != layout.glyphs.size()) {
      return false;
    }
  }
  return true;
}

} // namespace

Refusal submit(Core *core, std::int32_t drawAreaX1) {
  switch (spyro::menu_world::submit(core)) {
  case spyro::menu_world::Refusal::None:
    break;
  case spyro::menu_world::Refusal::ActorChain:
    return Refusal::ActorChain;
  case spyro::menu_world::Refusal::Particles:
    return Refusal::Particles;
  case spyro::menu_world::Refusal::Cyclorama:
    return Refusal::Cyclorama;
  case spyro::menu_world::Refusal::Environment:
    return Refusal::Environment;
  }

  // Read BEFORE the epilogue write below, so this frame's gate is the counter as the guest's own
  // epilogue left it at the end of the previous one.
  const menu_lighting::Lighting lighting = menu_lighting::read(core);
  const State state = readState(core, lighting);
  const Recipe recipe = spyro::pause_menu::derive(state);
  lucent::debug("render",
                "pause-menu: frameCounter={} page={} gui={} panel=({},{})..({},{}) border={} "
                "captions={}",
                state.frameCounter,
                static_cast<unsigned>(state.page),
                recipe.gui ? 1 : 0,
                recipe.panelX0,
                recipe.panelY0,
                recipe.panelX1,
                recipe.panelY1,
                recipe.border.size(),
                recipe.captions.size());
  if (!recipe.gui) {
    // The zero path's VRAM lifecycle: the menu's exit restores this save unconditionally.
    saveBorrowedTextures(core);
  } else {
    // The menu's own two arenas, where 0x8001A5E0 establishes them. The moby pair is what makes the
    // text visible: 0x80018880 walks [0x80075710] up to [0x800756FC], the builders step it down.
    const std::uint32_t primBase = core->mem_r32(kMenuPrimPoolBase);
    const std::uint32_t mobyBase = hudOtSaveBase(core);
    core->mem_w32(kMenuPrimPoolCursor, primBase);
    core->mem_w32(kMenuPrimPoolEnd, primBase + kMenuPrimPoolBytes);
    core->mem_w32(kMenuMobyArenaEnd, mobyBase);
    core->mem_w32(kMenuMobyArenaCursor, mobyBase);
    core->mem_w32(kMenuOverlayPrimCursor, 0u);
    // Already placed through `ui_anchor`, so RQ_2D_WIDE_FINAL: the queue must not re-centre them.
    ProducerScope producer(&core->rsub.producerScope, kProducerKey, "pause:menu");
    RenderQueue::Space2dScope authored(core->game->rq, RQ_2D_WIDE_FINAL);
    const auto panelColour = readPanelColourByte(core);
    if (!panelColour) {
      lucent::error("render",
                    "the guest's panel colour byte at 0x{:08X} is not `addiu $s4, $zero, imm`; "
                    "refusing to draw the pause panel with an assumed colour",
                    spyro::pause_menu::kPanelColourDefinitionPc);
      return Refusal::PanelColour;
    }
    lucent::debug("render",
                  "pause-menu panel colour: byte=0x{:02X} from guest 0x{:08X}",
                  *panelColour,
                  spyro::pause_menu::kPanelColourDefinitionPc);
    const std::int32_t shift = panelShift(core, recipe);
    menu_panel::submitPanel(core,
                            core->game->rq,
                            {recipe.panelX0, recipe.panelY0, recipe.panelX1, recipe.panelY1},
                            expandPanelColour(*panelColour),
                            shift);
    menu_panel::submitBorder(core, core->game->rq, recipe.border, drawAreaX1, shift);
    // The guest's AddPrim advances the prim cursor by each record it links: 6 words for the panel
    // and 5 for each line.
    core->mem_w32(kMenuPrimPoolCursor,
                  core->mem_r32(kMenuPrimPoolCursor) + kPanelPrimBytes +
                      static_cast<std::uint32_t>(recipe.border.size()) * kLinePrimBytes);
    if (!submitText(core, recipe)) {
      return Refusal::Text;
    }
    // Terminate the shaded queue, let 0x80018880 append the arena just filled, and let 0x80022A2C
    // draw it.
    core->mem_w32(kShadedMobyQueue, 0u);
    psx::cpu::dispatchGuestToReturn0(
        *core, kCopyHudMobys, psx::cpu::ExecutionBudget::currentTurn(*core), "pause-menu-text");
    lucent::debug(
        "render",
        "pause-menu text: shadedQueueHead=0x{:08X} firstEntry=0x{:08X} arenaCursor=0x{:08X} "
        "arenaEnd=0x{:08X} primCursor=0x{:08X}",
        core->mem_r32(kShadedMobyQueue),
        core->mem_r32(kShadedMobyQueue + 4u),
        core->mem_r32(kMenuMobyArenaCursor),
        core->mem_r32(kMenuMobyArenaEnd),
        core->mem_r32(kMenuPrimPoolCursor));
    if (spyro_field_actor_composition_submit(
            core, {.secondary = false, .shaded = true, .captionPass = true})) {
      return Refusal::ShadedActors;
    }
  }
  // The guest's epilogue write at 0x8001C65C: the counter advances on the way OUT, after the
  // picture is built, so the recipe above reads it before this.
  core->mem_w32(kMenuFrameCounter, state.frameCounter + 1u);
  return Refusal::None;
}

const char *refusalName(Refusal refusal) {
  switch (refusal) {
  case Refusal::None:
    return "none";
  case Refusal::ActorChain:
    return "actor chain producer 0x80019698 refused its atomic recipe";
  case Refusal::Particles:
    return "particles producer 0x800573C8 refused its atomic recipe";
  case Refusal::Cyclorama:
    return "cyclorama producer 0x80050BD0 refused its atomic recipe";
  case Refusal::Environment:
    return "environment producer 0x8002B9CC refused its atomic recipe";
  case Refusal::Text:
    return "pause-menu captions refused the HUD moby arena";
  case Refusal::ShadedActors:
    return "shaded actor producer 0x80022A2C refused its atomic recipe";
  case Refusal::PanelColour:
    return "the guest's panel colour byte at 0x8001A6C8 is not `addiu $s4, $zero, imm`";
  }
  return "unknown";
}

} // namespace spyro::pause_menu_scene
