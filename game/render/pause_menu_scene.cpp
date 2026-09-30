#include "pause_menu_scene.h"

#include "core.h"
#include "cutscene_scene_recipe.h"
#include "field_moby_lists.h"
#include "field_model_chain.h"
#include "fx_field_actor_composition.h"
#include "fx_field_cyclorama.h"
#include "fx_field_environment.h"
#include "fx_field_particles.h"
#include "game.h"
#include "guest_call.h"
#include "guest_globals.h"
#include "hud_text_builder.h"
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

// The handler's own globals, decoded from the shipping executable. Each is the address the handler
// itself reads, not a name this repository invented for it.

// [0x800758B8] IS THE MENU'S OWN FRAME COUNTER, and the port has to advance it because it took over
// the half of the guest that used to. Measured, not inferred: tools/probe_pause_gate_writer.py asks
// the IMAGE (not the sparse external/spyro-1/asm listing, which covers 17.2% of main RAM and
// reported "0 stores" for a word this very handler reads at 0x8001A410) and finds 23 lw sites and 6
// sw sites. Five of the writes are `sw $zero` inside the world renderer 0x8002B9CC -> 0x800258F0,
// at 0x8002C498, 0x8002C4B8, 0x8002C640 and 0x8002C79C. The sixth, and the only one that ever makes
// the word NON-zero, is 0x8001C65C: `lw` it, `addiu $v0,$v0,1`, `sw` it back — at 0x8001C65C, which
// is the EPILOGUE OF THIS HANDLER (0x8001A40C + 8840 = 0x8001C69C). So the guest's sequence is:
// frame 1 takes the world path, the world renderer zeroes the counter, and the epilogue leaves it
// 1; every later frame takes the menu path. This port replaced the handler, so it replaced the
// increment too — without it the counter is pinned at 0 forever, every frame is the transition
// frame, and the menu never draws. That is the whole defect, and it is why "exit 0" arrived with a
// picture of the world and no menu on it.
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
constexpr std::uint32_t kBorderLightingPhase = 0x800770F4u;
// s_8006d82c, the ramp 0x800169AC indexes at `ratio + 0xDC`. 0xDC = 220 and the ratio's largest
// value is 64, so 65 bytes is the whole domain the guest can ask for.
constexpr std::uint32_t kDirectionRampBase = 0x8006D82Cu;
constexpr std::uint32_t kDirectionRampOffset = 0xDCu;
constexpr std::size_t kDirectionRampLength = 65;
// 0x80019698's lifecycle clear, at the guest's own position immediately before the moby build: it
// terminates the previous screen's shaded queue. The FIELD arm reproduces the same write at
// 0x800720F4, and omitting it leaves the field HUD capacity consumed by a stale screen.
constexpr std::uint32_t kShadedMobyList = 0x800720F4u;
// 0x80018880, the guest's own "append the HUD mobys I just built to the shaded queue" leaf. The
// level-transition tally reaches the picture through exactly this pair, so there is one route for
// HUD text in this port rather than a second transcription of it.
constexpr std::uint32_t kCopyHudMobys = 0x80018880u;
// g_SonyImage.m_ShadedMobys' world queue head. Zeroed before the copy so the range the copy appends
// is a complete list and not the tail of a previous screen's.
constexpr std::uint32_t kShadedMobyQueue = 0x8006FCF4u + 0x2400u;

// The panel's producer identity. The panel and the border are the handler's own prims, so they are
// keyed by the handler's address; that is what puts this producer's prims in the same census row as
// the guest's, which is the point of the comparison.
constexpr std::uint32_t kProducerKey = 0x8001A40Cu;

// The captions' glyph mobys carry this specular index. It is not a per-screen choice: it is what
// the guest's shared builder 0x80017FE4 / 0x800181AC write, which is why the level-transition tally
// passes the same value.
constexpr std::uint8_t kCaptionShade = 2;

// The menu's own pools, from 0x8001A604-0x8001A650. `lui $a0,1 / ori $a0,0xc000` is 0x1C000 and
// `lui $a1,0xfffe / ori $a1,0x3e00` is 0xFFFE3E00; the two base pointers are read from 0x8008_5E8
// and 0x8008_5F0 through lui 0x8008 with offsets -0x7A18 and -0x7A10.
constexpr std::uint32_t kMenuPrimPoolBase = 0x800785E8u;
constexpr std::uint32_t kMenuMobyPoolBase = 0x800785F0u;
constexpr std::uint32_t kMenuPrimPoolCursor = 0x800757B0u;
constexpr std::uint32_t kMenuPrimPoolEnd = 0x80075780u;
constexpr std::uint32_t kMenuMobyArenaCursor = 0x80075710u;
constexpr std::uint32_t kMenuMobyArenaEnd = 0x800756FCu;
constexpr std::uint32_t kMenuOverlayPrimCursor = 0x800758B0u;
constexpr std::uint32_t kMenuPrimPoolBytes = 0x1C000u;
constexpr std::uint32_t kMenuMobyBaseAdjust = 0xFFFE3E00u;
// Record sizes, from the cursor steps the guest's own code performs: `DAT_800757b0 = puVar1 + 0x17`
// after a prim at puVar1[0x11] is 6 words, and 0x8001844C's `addiu $s0,$s0,0x14` is 5.
constexpr std::uint32_t kPanelPrimBytes = 6u * 4u;
constexpr std::uint32_t kLinePrimBytes = 5u * 4u;

State readState(Core *core) {
  std::array<std::uint8_t, kDirectionRampLength> ramp{};
  for (std::size_t i = 0; i < ramp.size(); ++i) {
    ramp[i] =
        core->mem_r8(kDirectionRampBase + kDirectionRampOffset + static_cast<std::uint32_t>(i));
  }
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
  state.lightingPhase = core->mem_r32(kBorderLightingPhase);
  state.directionRamp = ramp;
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

// The panel. 0x8001A7C4-0x8001A84C stores the colour byte 0xE0 in all three low bytes of the word
// and the GP0 command 0x2A in the fourth; 0x2A is an untextured 4-vertex quad whose bit 15 is the
// semi-transparency bit, so the panel is a dark translucent wash over the world rather than an
// opaque plate. Both facts are in the constant, not in a guess about which one it meant.
// The panel's colour byte is the immediate of the guest's own `addiu $s4,$zero,imm` at
// kPanelColourDefinitionPc (retail 0x40), read from the resident image; an unexpected word refuses.
std::optional<std::uint8_t> readPanelColourByte(Core *core) {
  return spyro::pause_menu::panelColourByte(
      core->mem_r32(spyro::pause_menu::kPanelColourDefinitionPc));
}

// THE PANEL'S ANCHOR CLASS, and where it comes from. `centred`, from the guest's own constants and
// not from looking at a picture: the panel is 0x8C..0x174 on the main and confirm pages and
// 0x54..0x1AC on the options page (0x8001A82C-0x8001A834, 0x8001A914-0x8001A920), and BOTH are
// symmetric about 0x100 = 256 = half of the guest's own 512. Its five border segments are the edges
// of that same box, so they carry the panel's class and not one of their own. The captions are
// authored INSIDE that box and move with it, which is why they are centred too and why nothing here
// is an edge element.
constexpr spyro::ui_anchor::Anchor kPanelAnchor = spyro::ui_anchor::Anchor::Centred;

// The ONE anchoring decision for the panel and its border. The border's five line records are the
// edges of the panel's box, so they take the panel's shift rather than being placed one by one: a
// vertical edge has zero width and is not a box the anchoring owner could place, and placing each
// edge by its own extent would be placing a rectangle by its corners. At 4:3 the shift is zero and
// every coordinate is the guest's own. A refused placement shifts nothing.
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

void submitPanel(Core *core,
                 RenderQueue &queue,
                 const Recipe &recipe,
                 std::uint8_t colourByte,
                 std::int32_t shift) {
  const std::uint32_t word = static_cast<std::uint32_t>(colourByte) |
                             (static_cast<std::uint32_t>(colourByte) << 8) |
                             (static_cast<std::uint32_t>(colourByte) << 16);
  const auto expand = [](std::uint32_t v) {
    return static_cast<unsigned char>((v << 3) | (v >> 2));
  };
  const unsigned char r = expand(word & 0x1Fu);
  const unsigned char g = expand((word >> 5) & 0x1Fu);
  const unsigned char b = expand((word >> 10) & 0x1Fu);
  const GpuState gpu = core->game->gpu;
  auto panel = spyro::pause_menu::placePanel(recipe, {gpu.s_off_x, gpu.s_off_y});
  panel.x0 += shift;
  panel.x1 += shift;
  const int xs[4] = {panel.x0, panel.x1, panel.x0, panel.x1};
  const int ys[4] = {panel.y0, panel.y0, panel.y1, panel.y1};
  const int us[4] = {};
  const int vs[4] = {};
  const unsigned char rs[4] = {r, r, r, r};
  const unsigned char gs[4] = {g, g, g, g};
  const unsigned char bs[4] = {b, b, b, b};
  queue.emitOrQueue(core,
                    1,
                    RQ_HUD,
                    RQ_OM_2D_FG,
                    4,
                    spyro::pause_menu::kPanelStp,
                    0,
                    xs,
                    ys,
                    nullptr,
                    nullptr,
                    us,
                    vs,
                    rs,
                    gs,
                    bs,
                    nullptr,
                    3,
                    0,
                    0,
                    0,
                    0,
                    gpu.s_tw_mx,
                    gpu.s_tw_my,
                    gpu.s_tw_ox,
                    gpu.s_tw_oy,
                    gpu.s_da_x0,
                    gpu.s_da_y0,
                    panel.x1,
                    gpu.s_da_y1,
                    gpu.s_tp_blend);
}

void submitBorder(Core *core,
                  RenderQueue &queue,
                  const Recipe &recipe,
                  std::int32_t drawAreaX1,
                  std::int32_t shift) {
  const GpuState gpu = core->game->gpu;
  for (const Segment &authored : recipe.border) {
    Segment segment = spyro::pause_menu::placeSegment(authored, {gpu.s_off_x, gpu.s_off_y});
    segment.x0 += shift;
    segment.x1 += shift;
    // NO endpoint-ordering test. A line is a line: the guest's box walks right-to-left on its
    // bottom edge and bottom-to-top on its left one, and an x0<x1 / y0<y1 guard here refused two of
    // the five edges while letting the other three through, which is how a box came out with one
    // side. Only the clip is a real constraint.
    if (segment.x1 >= drawAreaX1 || segment.x0 < gpu.s_da_x0 || segment.y1 < gpu.s_da_y0) {
      continue;
    }
    const spyro::pause_menu::Rgb start = spyro::pause_menu::borderColour(segment.shade0);
    const spyro::pause_menu::Rgb end = spyro::pause_menu::borderColour(segment.shade1);
    const int xs[4] = {segment.x0, segment.x1, segment.x0, segment.x1};
    const int ys[4] = {segment.y0, segment.y1, segment.y0, segment.y1};
    const int us[4] = {};
    const int vs[4] = {};
    const unsigned char rs[4] = {start.r, end.r, start.r, end.r};
    const unsigned char gs[4] = {start.g, end.g, start.g, end.g};
    const unsigned char bs[4] = {start.b, end.b, start.b, end.b};
    queue.emitOrQueue(core,
                      1,
                      RQ_HUD,
                      RQ_OM_2D_FG,
                      2,
                      0,
                      0,
                      xs,
                      ys,
                      nullptr,
                      nullptr,
                      us,
                      vs,
                      rs,
                      gs,
                      bs,
                      nullptr,
                      0,
                      0,
                      0,
                      0,
                      0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawAreaX1 - 1,
                      gpu.s_da_y1,
                      0);
  }
}

// The captions, through the two owners this port already has for the guest's own text: the layout
// half in hud_text_builder (0x80017FE4 fixed pitch, 0x800181AC proportional) and the shaded-moby
// pass 0x80022A2C that draws the glyphs. Every glyph is counted before the arena is written, so a
// caption that cannot fit refuses the whole panel instead of leaving half a menu behind.
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
  // ── The zero path: 0x800521C0, 0x80019698, 0x800573C8, 0x80050BD0, 0x8002B9CC ─────────────────
  // The same frame preparation the FIELD arm performs, because the two arms call the same
  // producers over the same world state; only the 2D layers differ, and the handler calls none of
  // them. g_SonyImage's shaded list is terminated at the guest's own position before the moby build
  // so the level transition's HUD mobys cannot leak into this screen.
  const auto background = spyro::cutscene_scene_recipe::read(core);
  spyro::cutscene_scene_recipe::prepareFrame(core, background);
  core->mem_w32(kShadedMobyList, 0u);
  spyro_field_build_moby_lists(core);
  if (const auto refusal = spyro_field_model_chain_submit(core)) {
    lucent::debug("render", "REFUSED 0x80019698: {}", refusal.detail);
    return Refusal::ActorChain;
  }
  if (const auto refusal = spyro_field_particles_submit(core)) {
    lucent::debug("render", "REFUSED 0x800573C8: {}", refusal.detail);
    return Refusal::Particles;
  }
  if (!spyro_field_cyclorama_submit(core)) {
    return Refusal::Cyclorama;
  }
  if (!spyro_field_environment_submit(core)) {
    return Refusal::Environment;
  }

  // ── The non-zero path: the panel, the border, the captions ─────────────────────────────────────
  // Read BEFORE the epilogue write below, so this frame's gate is the counter as the guest's own
  // epilogue left it at the end of the previous one.
  const State state = readState(core);
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
  if (recipe.gui) {
    // THE MENU'S OWN TWO ARENAS, established exactly where 0x8001A5E0 establishes them
    // (0x8001A604-0x8001A650), before anything is built in either. Both are guest state this
    // producer has to own, and the second is what makes the TEXT VISIBLE AT ALL: 0x80018880 — the
    // leaf that copies built captions to the shaded queue — walks the moby arena from [0x80075710]
    // UP to [0x800756FC], and 0x80017FE4 / 0x800181AC step [0x80075710] down. With the field's
    // values still in those two words the copy walked a range the menu never wrote, appended
    // nothing, and the shaded pass reported shaded_faces=0 — a MEASURED 0, not an absent one. The
    // first pair is the prim pool the panel and border are linked into.
    const std::uint32_t primBase = core->mem_r32(kMenuPrimPoolBase);
    const std::uint32_t mobyBase = core->mem_r32(kMenuMobyPoolBase) + kMenuMobyBaseAdjust;
    core->mem_w32(kMenuPrimPoolCursor, primBase);
    core->mem_w32(kMenuPrimPoolEnd, primBase + kMenuPrimPoolBytes);
    core->mem_w32(kMenuMobyArenaEnd, mobyBase);
    core->mem_w32(kMenuMobyArenaCursor, mobyBase);
    core->mem_w32(kMenuOverlayPrimCursor, 0u);
    // The panel and the border are this handler's own 2D prims, authored in the game's own
    // 512-wide space. This producer has already placed them through `ui_anchor`, so the queue must
    // NOT apply its own centring rule on top: RQ_2D_WIDE_FINAL means "this x is final", and the
    // reason it is now final is that the anchoring owner — not the framework's uniform rule —
    // decided which class each element is. At 4:3 both are the identity, so the 4:3 frame is the
    // guest's own.
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
    submitPanel(core, core->game->rq, recipe, *panelColour, shift);
    submitBorder(core, core->game->rq, recipe, drawAreaX1, shift);
    // The guest's AddPrim (0x800168DC) advances the prim cursor by each record it links: 6 words
    // for the GP0(0x2A) panel and 5 for each 0x8001844C line. Reproduced so the pool position the
    // next frame starts from is the one the guest would leave.
    core->mem_w32(kMenuPrimPoolCursor,
                  core->mem_r32(kMenuPrimPoolCursor) + kPanelPrimBytes +
                      static_cast<std::uint32_t>(recipe.border.size()) * kLinePrimBytes);
    if (!submitText(core, recipe)) {
      return Refusal::Text;
    }
    // The guest's own route from built captions to drawn captions, identical to the
    // level-transition tally's: terminate the shaded queue, let 0x80018880 append the arena it just
    // filled, and let 0x80022A2C draw it.
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
    if (spyro_field_actor_composition_submit(core, {.secondary = false, .shaded = true})) {
      return Refusal::ShadedActors;
    }
  }
  // The epilogue this port inherited with the handler: 0x8001C65C. It is the guest's own state
  // write at the guest's own position, and it is the one write this producer cannot delegate — the
  // guest advances it on the way OUT, after the picture is built, and only the replaced body did.
  // It is a lifecycle write, not a source of picture state: the recipe above is read before it.
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
