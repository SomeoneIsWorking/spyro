#include "fairy_menu_recipe.h"

namespace spyro::fairy_menu {
namespace {

// Every string address below was found in the resident SCUS_942.28 image by its NUL-terminated
// bytes (one match each), not copied from the decompiler's symbol names; the recipe test proves
// each one against a RAM dump when one is supplied.
constexpr std::uint32_t kStrHiSpyro = 0x80010BA8u;          // "HI SPYRO,"
constexpr std::uint32_t kStrSaveGame = 0x80010BB4u;         // "SAVE GAME"
constexpr std::uint32_t kStrReplayDragon = 0x80010BC0u;     // "REPLAY DRAGON"
constexpr std::uint32_t kStrContinue = 0x80010B6Cu;         // "CONTINUE"
constexpr std::uint32_t kStrNoSaveFile = 0x80010BD0u;       // "NO SAVE FILE"
constexpr std::uint32_t kStrPleaseRestart = 0x80010BE0u;    // "PLEASE RESTART WITH"
constexpr std::uint32_t kStrAMemoryCardTo = 0x80010BF4u;    // "A MEMORY CARD TO"
constexpr std::uint32_t kStrEnableSaves = 0x80010C08u;      // "ENABLE GAME SAVES."
constexpr std::uint32_t kStrSaving = 0x80010C1Cu;           // "SAVING..."
constexpr std::uint32_t kStrNoMemoryCard = 0x80010C28u;     // "NO MEMORY CARD"
constexpr std::uint32_t kStrPleaseInsert = 0x80010C38u;     // "PLEASE INSERT THE"
constexpr std::uint32_t kStrMemoryCardWith = 0x80010C4Cu;   // "MEMORY CARD WITH"
constexpr std::uint32_t kStrThisGameSaveFile = 0x80010C60u; // "THIS GAME SAVE FILE"
constexpr std::uint32_t kStrSaveError = 0x80010C74u;        // "SAVE ERROR"
constexpr std::uint32_t kStrPleaseCheck = 0x80010C80u;      // "PLEASE CHECK THAT"
constexpr std::uint32_t kStrTheMemoryCard = 0x80010C94u;    // "THE MEMORY CARD"
constexpr std::uint32_t kStrStillInPlace = 0x80010CA4u;     // "IS STILL IN PLACE."
constexpr std::uint32_t kStrSaveFailed = 0x80010CB8u;       // "SAVE FAILED"
constexpr std::uint32_t kStrGameSaved = 0x80010CC4u;        // "GAME SAVED"
constexpr std::uint32_t kStrRetry = 0x80075620u;            // "RETRY"
constexpr std::uint32_t kStrAbort = 0x80075628u;            // "ABORT"
constexpr std::uint32_t kStrSlot1 = 0x80075630u;            // "SLOT 1"
constexpr std::uint32_t kStrSlot2 = 0x80075638u;            // "SLOT 2"

// The two spacing vectors and the depths the handler passes. The title/option captions use
// {16, 1, 0x1400} at z 0x1100; the body text of pages 1, 3, 4 and 5 uses {14, 1, 0x1600} at z
// 0x1400; the slot caption uses {13, 1, 0x1A00} at z 0x1600.
constexpr pause_menu::Spacing kTitleSpacing{16, 1, 0x1400};
constexpr pause_menu::Spacing kBodySpacing{14, 1, 0x1600};
constexpr pause_menu::Spacing kSlotSpacing{13, 1, 0x1A00};
constexpr std::int32_t kTitleDepth = 0x1100;
constexpr std::int32_t kBodyDepth = 0x1400;
constexpr std::int32_t kSlotDepth = 0x1600;
constexpr std::int32_t kTitleSpaceWidth = 18;
constexpr std::int32_t kBodySpaceWidth = 16;
constexpr std::int32_t kSlotSpaceWidth = 14;
constexpr std::int32_t kSlotY = 29;
constexpr std::int32_t kSlot2X = 230; // the guest does not add the menu offset to it (draw.c:2307)
constexpr std::int32_t kSlot1X = 48;
constexpr std::int32_t kBodyLineStep = 17;

// Pages 0 and 6 are the only ones that read the selection.
constexpr std::uint32_t kMainOptions = 3;
constexpr std::uint32_t kFailedOptions = 2;
constexpr std::uint32_t kPageSlotsStart = 2; // pages above 1 also show the used card slot

Caption title(std::uint32_t text, std::int32_t x, std::int32_t y) {
  return {text, x, y, kTitleDepth, kTitleSpacing, kTitleSpaceWidth};
}

Caption body(std::uint32_t text, std::int32_t x, std::int32_t y) {
  return {text, x, y, kBodyDepth, kBodySpacing, kBodySpaceWidth};
}

// Pages 1, 3, 4 and 5 are a wobbling title, then three body lines under it.
void titledBody(Recipe &recipe,
                std::int32_t offset,
                Caption head,
                std::array<std::uint32_t, 3> lines,
                std::array<std::int32_t, 3> xs) {
  constexpr std::int32_t kFirstBodyY = 76; // the title's y (50) plus 26
  recipe.captions.push_back(head);
  recipe.wobbled = recipe.captions.size() - 1;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    recipe.captions.push_back(
        body(lines[i], offset + xs[i], kFirstBodyY + static_cast<std::int32_t>(i) * kBodyLineStep));
  }
}

// A page that is one wobbling caption and nothing else.
void single(Recipe &recipe, Caption caption) {
  recipe.captions.push_back(caption);
  recipe.wobbled = 0;
}

bool addPage(Recipe &recipe, const State &state) {
  const std::int32_t o = state.offsetX;
  switch (state.page) {
  case 0: {
    if (state.selected >= kMainOptions) {
      return false;
    }
    recipe.captions.push_back(title(kStrHiSpyro, o + 64, 50));
    recipe.captions.push_back(title(kStrSaveGame, o + 76, 74));
    recipe.captions.push_back(title(kStrReplayDragon, o + 76, 93));
    recipe.captions.push_back(title(kStrContinue, o + 76, 112));
    recipe.wobbled = 1 + state.selected;
    return true;
  }
  case 1:
    titledBody(recipe,
               o,
               title(kStrNoSaveFile, o + 82, 50),
               {kStrPleaseRestart, kStrAMemoryCardTo, kStrEnableSaves},
               {45, 67, 56});
    return true;
  case 2:
    single(recipe, title(kStrSaving, o + 106, 82));
    return true;
  case 3:
    titledBody(recipe,
               o,
               title(kStrNoMemoryCard, o + 66, 50),
               {kStrPleaseInsert, kStrMemoryCardWith, kStrThisGameSaveFile},
               {59, 67, 45});
    return true;
  case 4:
    titledBody(recipe,
               o,
               title(kStrNoSaveFile, o + 84, 50),
               {kStrPleaseInsert, kStrMemoryCardWith, kStrThisGameSaveFile},
               {59, 67, 45});
    return true;
  case 5:
    titledBody(recipe,
               o,
               title(kStrSaveError, o + 94, 50),
               {kStrPleaseCheck, kStrTheMemoryCard, kStrStillInPlace},
               {59, 73, 54});
    return true;
  case 6: {
    if (state.selected >= kFailedOptions) {
      return false;
    }
    recipe.captions.push_back(title(kStrSaveFailed, o + 86, 53));
    recipe.captions.push_back(title(kStrRetry, o + 135, 93));
    recipe.captions.push_back(title(kStrAbort, o + 135, 112));
    recipe.wobbled = 1 + state.selected;
    return true;
  }
  case 7:
    single(recipe, title(kStrGameSaved, o + 94, 82));
    return true;
  default:
    return false;
  }
}

void addSlotCaption(Recipe &recipe, const State &state) {
  if (state.page < kPageSlotsStart) {
    return;
  }
  const bool first = state.cardSlot == 0;
  recipe.captions.push_back({first ? kStrSlot1 : kStrSlot2,
                             first ? state.offsetX + kSlot1X : kSlot2X,
                             kSlotY,
                             kSlotDepth,
                             kSlotSpacing,
                             kSlotSpaceWidth});
}

// func_8001860C's four 0x8001844C calls, in its order: top, right, bottom, left.
void addBorder(Recipe &recipe, const State &state) {
  const pause_menu::PanelRect &p = recipe.panel;
  const pause_menu::Segment edges[] = {
      {p.x0, p.y0, p.x1, p.y0, 0, 0},
      {p.x1, p.y0, p.x1, p.y1, 0, 0},
      {p.x1, p.y1, p.x0, p.y1, 0, 0},
      {p.x0, p.y1, p.x0, p.y0, 0, 0},
  };
  for (const pause_menu::Segment &edge : edges) {
    recipe.border.push_back(pause_menu::litSegment(edge, state.directionRamp, state.lightingPhase));
  }
}

} // namespace

Recipe derive(const State &state) {
  Recipe recipe;
  if (state.state != 1u) {
    return recipe;
  }
  recipe.kind = Kind::Refused;
  if (state.page >= kPageCount) {
    return recipe;
  }
  const BoxRecord &box = state.boxes[state.page];
  recipe.panel = {state.offsetX + box.x, box.y, state.offsetX + box.x2, box.y2};
  if (!addPage(recipe, state)) {
    recipe.captions.clear();
    recipe.wobbled.reset();
    return recipe;
  }
  addSlotCaption(recipe, state);
  addBorder(recipe, state);
  recipe.kind = Kind::Dialogue;
  return recipe;
}

} // namespace spyro::fairy_menu
