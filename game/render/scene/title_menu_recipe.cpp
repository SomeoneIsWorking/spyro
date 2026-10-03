#include "title_menu_recipe.h"

#include <algorithm>
#include <cstdlib>

namespace spyro::title_menu_recipe {
namespace {

constexpr uint32_t kGray = 0;
constexpr uint32_t kYellow = 1;

constexpr int32_t kMainBorder = 1;
constexpr int32_t kEmptySave = 8;
constexpr int32_t kBlankSaveBox = 9;
constexpr int32_t kOverwriteGame = 10;
constexpr int32_t kDragonCountZero = 13;
constexpr int32_t kDragonCountLogo = 23;
constexpr int32_t kAccessingMemCard = 24;
constexpr int32_t kNoMemCard = 25;
constexpr int32_t kMemCardUnavailable = 26;
constexpr int32_t kWarning = 27;
constexpr int32_t kYouWillNotBeAbleTo = 28;
constexpr int32_t kSaveYourProgressUnless = 29;
constexpr int32_t kYouCreateASaveFileNow = 30;
constexpr int32_t kMemoryCardInSlot1 = 31;
constexpr int32_t kIsNotFormatted = 33;
constexpr int32_t kFormatItNow = 34;
constexpr int32_t kAnySaveDataOnThis = 35;
constexpr int32_t kMemoryCardWillBeLost = 36;
constexpr int32_t kFormatting = 37;
constexpr int32_t kFormatComplete = 38;
constexpr int32_t kFormatFailed = 39;
constexpr int32_t kDoesNotHaveASaveFile = 40;
constexpr int32_t kCreateSaveFileNow = 41;
constexpr int32_t kCreatingSaveFile = 42;
constexpr int32_t kIsFullThisGame = 43;
constexpr int32_t kRequiresOneFreeBlock = 44;
constexpr int32_t kUnableToCreateSaveFile = 45;
constexpr int32_t kSelectSlotForNewGame = 46;
constexpr int32_t kSelectSlotToLoadGame = 47;
constexpr int32_t kUsingCardInSlot1 = 49;
constexpr int32_t kSelectMemoryCard = 51;
constexpr int32_t kStartNewGame = 52;
constexpr int32_t kRetry = 53;
constexpr int32_t kCancel = 54;
constexpr int32_t kFormatCard = 55;
constexpr int32_t kCreateFile = 56;
constexpr int32_t kContinue = 57;
constexpr int32_t kLoadGame = 58;
constexpr int32_t kNewGame = 59;
constexpr int32_t kSlot1 = 60;
constexpr int32_t kSlot2 = 61;
constexpr int32_t kOverwrite = 62;
constexpr int32_t kToCreateASaveFile = 63;

bool blinkOn(uint32_t subTick) {
  return (subTick & 0xfu) < 8u;
}

uint32_t optionStyle(const Mode1Input &input, uint32_t option) {
  return input.optionSelected == option && blinkOn(input.subTick) ? kYellow : kGray;
}

void appendOptions(Recipe &recipe,
                   const Mode1Input &input,
                   int32_t leftSprite,
                   int32_t rightSprite) {
  if (input.subTick <= 7u) {
    return;
  }
  recipe.append(128, 88, leftSprite, optionStyle(input, 0));
  recipe.append(256, 88, rightSprite, optionStyle(input, 1));
}

void appendCardFooter(Recipe &recipe, const Mode1Input &input) {
  recipe.append(128, 106, input.cardSelected + kUsingCardInSlot1, kGray);
}

void appendMode2Slots(Recipe &recipe, const Mode2Input &input) {
  for (size_t i = 0; i < input.slots.size(); ++i) {
    const auto &slot = input.slots[i];
    const int32_t slotX = 140 + static_cast<int32_t>(i) * 80;
    if (!slot.occupied) {
      const bool dim = (input.state < 3u && input.optionSelected != static_cast<uint32_t>(i)) ||
                       (input.state >= 3u && input.state < 5u);
      recipe.append(slotX, 38, kEmptySave, dim ? 3u : kGray);
      continue;
    }

    const bool dim =
        (input.state < 4u && input.optionSelected != static_cast<uint32_t>(i)) || input.state == 4u;
    const uint32_t style = dim ? 3u : kGray;
    recipe.append(slotX, 38, slot.homeworldSprite, style);
    recipe.append(slotX + 44, 22, kDragonCountLogo, style);
    recipe.append(
        slotX + 28, 22, static_cast<int32_t>(slot.dragonCount % 10u) + kDragonCountZero, style);
    if (slot.dragonCount > 9u) {
      recipe.append(
          slotX + 12, 22, static_cast<int32_t>(slot.dragonCount / 10u) + kDragonCountZero, style);
    }
  }
}

} // namespace

void Recipe::append(int32_t x, int32_t y, int32_t sprite, uint32_t style) {
  if (size >= commands.size()) {
    std::abort();
  }
  commands[size++] = {.x = x, .y = y, .sprite = sprite, .style = style};
}

Recipe buildMode1(const Mode1Input &input) {
  Recipe recipe;
  recipe.append(108, 9, kMainBorder, kGray);
  recipe.append(255, 9, -kMainBorder, kGray);

  switch (input.substate) {
  case 0:
    recipe.append(128, 46, kAccessingMemCard, kGray);
    break;
  case 1:
    recipe.append(128, 46, kAccessingMemCard, kGray);
    if (input.optionSelected != 0u) {
      appendCardFooter(recipe, input);
    }
    break;
  case 2:
    recipe.append(128, 46, kNoMemCard, kGray);
    appendOptions(recipe, input, kStartNewGame, kRetry);
    appendCardFooter(recipe, input);
    break;
  case 3:
    recipe.append(128, 46, kMemCardUnavailable, kGray);
    appendOptions(recipe, input, kStartNewGame, kRetry);
    appendCardFooter(recipe, input);
    break;
  case 4:
    recipe.append(128, 22, kWarning, kGray);
    recipe.append(128, 38, kYouWillNotBeAbleTo, kGray);
    recipe.append(128, 54, kSaveYourProgressUnless, kGray);
    recipe.append(128, 70, kYouCreateASaveFileNow, kGray);
    appendOptions(recipe, input, kStartNewGame, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 5:
    recipe.append(128, 30, input.cardSelected + kMemoryCardInSlot1, kGray);
    recipe.append(128, 46, kIsNotFormatted, kGray);
    recipe.append(128, 62, kFormatItNow, kGray);
    appendOptions(recipe, input, kFormatCard, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 6:
    recipe.append(128, 30, kWarning, kGray);
    recipe.append(128, 46, kAnySaveDataOnThis, kGray);
    recipe.append(128, 62, kMemoryCardWillBeLost, kGray);
    appendOptions(recipe, input, kFormatCard, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 7:
    recipe.append(128, 46, kFormatting, kGray);
    break;
  case 8:
    recipe.append(128, 46, kFormatComplete, kGray);
    if (input.subTick > 7u) {
      recipe.append(128, 88, kContinue, blinkOn(input.subTick) ? kYellow : kGray);
    }
    appendCardFooter(recipe, input);
    break;
  case 9:
    recipe.append(128, 46, kFormatFailed, kGray);
    appendOptions(recipe, input, kRetry, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 10:
    recipe.append(128, 30, input.cardSelected + kMemoryCardInSlot1, kGray);
    recipe.append(128, 46, kDoesNotHaveASaveFile, kGray);
    recipe.append(128, 62, kCreateSaveFileNow, kGray);
    appendOptions(recipe, input, kCreateFile, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 11:
    recipe.append(128, 46, kCreatingSaveFile, kGray);
    appendCardFooter(recipe, input);
    break;
  case 12:
    recipe.append(128, 22, input.cardSelected + kMemoryCardInSlot1, kGray);
    recipe.append(128, 38, kIsFullThisGame, kGray);
    recipe.append(128, 54, kRequiresOneFreeBlock, kGray);
    recipe.append(128, 70, kToCreateASaveFile, kGray);
    if (input.subTick > 7u) {
      recipe.append(128, 88, kContinue, blinkOn(input.subTick) ? kYellow : kGray);
    }
    appendCardFooter(recipe, input);
    break;
  case 13:
    recipe.append(128, 46, kUnableToCreateSaveFile, kGray);
    appendOptions(recipe, input, kRetry, kCancel);
    appendCardFooter(recipe, input);
    break;
  case 15:
    recipe.append(128, 46, kSelectMemoryCard, kGray);
    appendOptions(recipe, input, kSlot1, kSlot2);
    break;
  default:
    // The guest has no case 14 and no default body. Its decorative borders
    // remain, so an unlisted substate is a valid two-command recipe.
    break;
  }
  return recipe;
}

Recipe buildMode2(const Mode2Input &input) {
  Recipe recipe;
  if (input.state < 5u) {
    recipe.append(108, 9, kMainBorder, kGray);
    recipe.append(255, 9, -kMainBorder, kGray);
    if (input.state > 0u) {
      appendMode2Slots(recipe, input);
    }
  } else {
    recipe.append(108, input.slideY, kMainBorder, kGray);
    recipe.append(255, input.slideY, -kMainBorder, kGray);
  }

  const bool blink = blinkOn(input.subTick);
  const int32_t selectedSlotX = 140 + static_cast<int32_t>(input.optionSelected) * 80;
  switch (input.state) {
  case 1:
    if (blink) {
      recipe.append(selectedSlotX, 38, kBlankSaveBox, kGray);
    }
    recipe.append(128, 88, kNewGame, kYellow);
    recipe.append(256, 88, kLoadGame, kGray);
    recipe.append(128, 106, kSelectSlotForNewGame, kGray);
    break;
  case 2:
    recipe.append(selectedSlotX, 38, kOverwriteGame, kGray);
    recipe.append(selectedSlotX, 38, kBlankSaveBox, kGray);
    recipe.append(128, 88, kOverwrite, input.secondaryOption == 0u && blink ? kYellow : kGray);
    recipe.append(256, 88, kCancel, input.secondaryOption == 1u && blink ? kYellow : kGray);
    break;
  case 3:
    if (blink) {
      recipe.append(selectedSlotX, 38, kBlankSaveBox, kGray);
    }
    recipe.append(128, 88, kNewGame, kGray);
    recipe.append(256, 88, kLoadGame, kYellow);
    recipe.append(128, 106, kSelectSlotToLoadGame, kGray);
    break;
  case 4:
    recipe.append(128, 88, kNewGame, input.optionSelected == 0u && blink ? kYellow : kGray);
    recipe.append(256, 88, kLoadGame, input.optionSelected == 1u && blink ? kYellow : kGray);
    recipe.append(128, 106, input.cardSelected + kUsingCardInSlot1, kGray);
    break;
  default:
    break;
  }
  return recipe;
}

bool sameCommands(const Recipe &expected, const Recipe &observed, size_t &mismatch) {
  const size_t common = std::min(expected.size, observed.size);
  for (mismatch = 0; mismatch < common; ++mismatch) {
    if (expected.commands[mismatch] != observed.commands[mismatch]) {
      return false;
    }
  }
  mismatch = common;
  return expected.size == observed.size;
}

} // namespace spyro::title_menu_recipe
