#include "fairy_menu_scene.h"

#include "core.h"
#include "fairy_menu_recipe.h"
#include "field_actor_composition.h"
#include "game.h"
#include "guest_call.h"
#include "hud_text_builder.h"
#include "menu_lighting.h"
#include "menu_panel_submit.h"
#include "menu_world_pass.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "screen_border.h"
#include "screen_border_recipe.h"
#include "ui_anchor.h"

#include <algorithm>
#include <lucent/log.h>
#include <string>
#include <vector>

namespace spyro::fairy_menu_scene {
namespace {

using spyro::fairy_menu::Kind;

// g_FairyCutscene (fairy.h) starts at 0x80078D00 and its fields are consecutive words.
constexpr std::uint32_t kFairyBase = 0x80078D00u;
constexpr std::uint32_t kFairyState = kFairyBase + 0x00u;
constexpr std::uint32_t kFairyTimer = kFairyBase + 0x04u;
constexpr std::uint32_t kFairySelected = kFairyBase + 0x08u;
constexpr std::uint32_t kFairyPage = kFairyBase + 0x0Cu;
constexpr std::uint32_t kFairyOffsetX = kFairyBase + 0x10u;
constexpr std::uint32_t kFairyCardSlot = kFairyBase + 0x18u;

// The head of 0x8001D718 shrinks the HUD moby arena to 0x1BA00 and points the arena cursor at the
// new end when D_800756FC == g_Buffers.m_HudOTStart, which is every other frame.
constexpr std::uint32_t kMobyArenaEnd = 0x800756FCu;
constexpr std::uint32_t kMobyArenaCursor = 0x80075710u;
constexpr std::uint32_t kPrimPoolCursor = 0x800757B0u;
constexpr std::uint32_t kHudOtStart = 0x800785F0u; // g_Buffers.m_HudOTStart
constexpr std::uint32_t kShrunkArenaBytes = 0x1BA00u;

constexpr std::uint32_t kShadedMobyQueue = 0x800720F4u;
constexpr std::uint32_t kCopyHudMobys = 0x80018880u;
constexpr std::uint32_t kProducerKey = 0x8001D718u;
constexpr std::size_t kMaxCaptionLength = 64;

// The dialogue box sits on the side of the screen the camera is not looking at and is authored in
// the guest's 512-wide space, symmetric about neither edge: its class is the pause panel's.
constexpr spyro::ui_anchor::Anchor kBoxAnchor = spyro::ui_anchor::Anchor::Centred;

fairy_menu::State readState(Core *core, const menu_lighting::Lighting &lighting) {
  fairy_menu::State state;
  state.state = core->mem_r32(kFairyState);
  state.timer = core->mem_r32(kFairyTimer);
  state.selected = core->mem_r32(kFairySelected);
  state.page = core->mem_r32(kFairyPage);
  state.offsetX = static_cast<std::int32_t>(core->mem_r32(kFairyOffsetX));
  state.cardSlot = core->mem_r32(kFairyCardSlot);
  for (std::uint32_t page = 0; page < fairy_menu::kPageCount; ++page) {
    const std::uint32_t at = fairy_menu::kBoxTable + page * 8u;
    state.boxes[page] = {core->mem_r16s(at),
                         core->mem_r16s(at + 2u),
                         core->mem_r16s(at + 4u),
                         core->mem_r16s(at + 6u)};
  }
  state.lightingPhase = lighting.phase;
  state.directionRamp = lighting.ramp;
  return state;
}

std::string readString(Core *core, std::uint32_t address) {
  std::string out;
  for (std::uint32_t i = 0; i < kMaxCaptionLength; ++i) {
    const std::uint8_t ch = core->mem_r8(address + i);
    if (ch == 0u) {
      break;
    }
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

void shrinkArenaLikeTheGuest(Core *core) {
  if (core->mem_r32(kMobyArenaEnd) != core->mem_r32(kHudOtStart)) {
    return;
  }
  const std::uint32_t shrunk = core->mem_r32(kPrimPoolCursor) + kShrunkArenaBytes;
  core->mem_w32(kMobyArenaEnd, shrunk);
  core->mem_w32(kMobyArenaCursor, shrunk);
}

// Builds every caption into the HUD arena in the guest's order and wobbles the recipe's caption the
// way the guest does: its glyphs, last character first, one step of 12 per glyph.
bool buildCaptions(Core *core, const fairy_menu::State &state, const fairy_menu::Recipe &recipe) {
  std::size_t glyphs = 0;
  std::vector<std::string> texts;
  for (const fairy_menu::Caption &caption : recipe.captions) {
    texts.push_back(readString(core, caption.text));
    if (texts.back().empty()) {
      return false;
    }
    glyphs += texts.back().size();
  }
  if (!spyro::hud_text::fits(core, glyphs)) {
    return false;
  }
  std::vector<std::uint32_t> wobbled;
  for (std::size_t i = 0; i < recipe.captions.size(); ++i) {
    const fairy_menu::Caption &caption = recipe.captions[i];
    const auto layout =
        spyro::hud_text::layoutCaption(texts[i],
                                       {caption.x, caption.y, caption.z},
                                       {caption.spacing.x, caption.spacing.y, caption.spacing.z},
                                       caption.spaceWidth);
    auto written = spyro::hud_text::append(core, layout, fairy_menu::kCaptionShade);
    if (written.size() != layout.glyphs.size()) {
      return false;
    }
    if (recipe.wobbled == i) {
      wobbled = std::move(written);
    }
  }
  // g_HudMobys is the arena's lowest address and the builder steps it down per glyph, so the
  // guest's `curMoby++` loop visits the wobbled caption's glyphs last character first.
  std::reverse(wobbled.begin(), wobbled.end());
  spyro::hud_text::wobbleScaled(core,
                                wobbled,
                                static_cast<std::int32_t>(state.timer) * 4,
                                {fairy_menu::kWobbleMultiplier, fairy_menu::kWobbleShift});
  return true;
}

std::int32_t boxShift(Core *core, const pause_menu::PanelRect &panel) {
  return spyro::ui_anchor::placeAndReport({"fairy-dialogue"},
                                          kBoxAnchor,
                                          panel.x0,
                                          panel.x1 - panel.x0,
                                          spyro::ui_anchor::frame(core))
      .offset;
}

Refusal submitDialogue(Core *core,
                       const fairy_menu::State &state,
                       const fairy_menu::Recipe &recipe,
                       std::int32_t drawAreaX1) {
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "fairy:dialogue");
  // The box and its outline are authored in the guest's 512-wide space and placed by the anchoring
  // owner above, so the queue must not centre them again.
  RenderQueue::Space2dScope authored(core->game->rq, RQ_2D_WIDE_FINAL);
  const std::int32_t shift = boxShift(core, recipe.panel);
  menu_panel::submitPanel(
      core,
      core->game->rq,
      recipe.panel,
      {fairy_menu::kPanelColour, fairy_menu::kPanelColour, fairy_menu::kPanelColour},
      shift);
  menu_panel::submitBorder(core, core->game->rq, recipe.border, drawAreaX1, shift);
  if (!buildCaptions(core, state, recipe)) {
    return Refusal::Text;
  }
  core->mem_w32(kShadedMobyQueue, 0u);
  psx::cpu::dispatchGuestToReturn0(
      *core, kCopyHudMobys, psx::cpu::ExecutionBudget::currentTurn(*core), "fairy-dialogue-text");
  if (spyro_field_actor_composition_submit(
          core, {.secondary = false, .shaded = true, .captionPass = true})) {
    return Refusal::ShadedActors;
  }
  return Refusal::None;
}

Refusal worldRefusal(spyro::menu_world::Refusal refusal) {
  switch (refusal) {
  case spyro::menu_world::Refusal::ActorChain:
    return Refusal::ActorChain;
  case spyro::menu_world::Refusal::Particles:
    return Refusal::Particles;
  case spyro::menu_world::Refusal::Cyclorama:
    return Refusal::Cyclorama;
  case spyro::menu_world::Refusal::Environment:
    return Refusal::Environment;
  case spyro::menu_world::Refusal::None:
    break;
  }
  return Refusal::None;
}

} // namespace

Refusal submit(Core *core, std::int32_t drawAreaX1) {
  shrinkArenaLikeTheGuest(core);
  if (const Refusal refusal = worldRefusal(spyro::menu_world::submit(core));
      refusal != Refusal::None) {
    return refusal;
  }
  const menu_lighting::Lighting lighting = menu_lighting::read(core);
  const fairy_menu::State state = readState(core, lighting);
  const fairy_menu::Recipe recipe = fairy_menu::derive(state);
  lucent::debug("render",
                "fairy-menu: state={} page={} selected={} offsetX={} kind={} captions={}",
                state.state,
                state.page,
                state.selected,
                state.offsetX,
                static_cast<int>(recipe.kind),
                recipe.captions.size());
  if (recipe.kind == Kind::Refused) {
    return Refusal::Page;
  }
  if (recipe.kind == Kind::Dialogue) {
    if (const Refusal refusal = submitDialogue(core, state, recipe, drawAreaX1);
        refusal != Refusal::None) {
      return refusal;
    }
  }
  // 0x80018F30 is called unconditionally at the end of the handler: it steps the bar height and
  // draws the two bars when they are up.
  const auto border = spyro::screen_border::stage(core);
  if (!spyro::screen_border::submit(core, core->game->rq, border)) {
    return Refusal::ScreenBorder;
  }
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
  case Refusal::Page:
    return "the fairy's dialogue page or selection is outside the handler's own switch";
  case Refusal::Text:
    return "the fairy's captions refused the HUD moby arena";
  case Refusal::ShadedActors:
    return "shaded actor producer 0x80022A2C refused its atomic recipe";
  case Refusal::ScreenBorder:
    return "screen border producer 0x80018F30 refused its atomic recipe";
  }
  return "unknown";
}

} // namespace spyro::fairy_menu_scene
