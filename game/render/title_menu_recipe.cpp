#include "title_menu_recipe.h"

#include <algorithm>
#include <cstdlib>

namespace spyro::title_menu_recipe {
namespace {

constexpr uint32_t kGray = 0;
constexpr uint32_t kYellow = 1;

constexpr int32_t kMainBorder = 1;
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
constexpr int32_t kUsingCardInSlot1 = 49;
constexpr int32_t kSelectMemoryCard = 51;
constexpr int32_t kStartNewGame = 52;
constexpr int32_t kRetry = 53;
constexpr int32_t kCancel = 54;
constexpr int32_t kFormatCard = 55;
constexpr int32_t kCreateFile = 56;
constexpr int32_t kContinue = 57;
constexpr int32_t kSlot1 = 60;
constexpr int32_t kSlot2 = 61;
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
