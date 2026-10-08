#include "title_menu_recipe.h"

#include <cstdio>
#include <initializer_list>

using spyro::title_menu_recipe::Mode1Input;
using spyro::title_menu_recipe::Mode2Input;
using spyro::title_menu_recipe::Mode2Slot;
using spyro::title_menu_recipe::Recipe;
using spyro::title_menu_recipe::SpriteCommand;

namespace {

int failures = 0;

void expectRecipe(const char *name,
                  const Recipe &actual,
                  std::initializer_list<SpriteCommand> expected) {
  if (actual.size != expected.size()) {
    std::fprintf(stderr,
                 "title_menu_recipe: %s size: got %zu expected %zu\n",
                 name,
                 actual.size,
                 expected.size());
    ++failures;
    return;
  }
  size_t i = 0;
  for (const auto &command : expected) {
    if (actual.commands[i] != command) {
      const auto &got = actual.commands[i];
      std::fprintf(stderr,
                   "title_menu_recipe: %s command %zu: got (%d,%d,%d,%u) expected "
                   "(%d,%d,%d,%u)\n",
                   name,
                   i,
                   got.x,
                   got.y,
                   got.sprite,
                   got.style,
                   command.x,
                   command.y,
                   command.sprite,
                   command.style);
      ++failures;
      return;
    }
    ++i;
  }
}

void expect(const char *name,
            const Mode1Input &input,
            std::initializer_list<SpriteCommand> expected) {
  expectRecipe(name, spyro::title_menu_recipe::buildMode1(input), expected);
}

void expectMode2(const char *name,
                 const Mode2Input &input,
                 std::initializer_list<SpriteCommand> expected) {
  expectRecipe(name, spyro::title_menu_recipe::buildMode2(input), expected);
}

constexpr SpriteCommand cmd(int32_t x, int32_t y, int32_t sprite, uint32_t style = 0) {
  return {.x = x, .y = y, .sprite = sprite, .style = style};
}

} // namespace

int main() {
  constexpr SpriteCommand leftBorder = cmd(108, 9, 1);
  constexpr SpriteCommand rightBorder = cmd(255, 9, -1);

  expect("load cards", {0, 0, 0, 0}, {leftBorder, rightBorder, cmd(128, 46, 24)});
  expect("read card pending", {1, 0, 0, 1}, {leftBorder, rightBorder, cmd(128, 46, 24)});
  expect("read selected card",
         {1, 1, 0, 1},
         {leftBorder, rightBorder, cmd(128, 46, 24), cmd(128, 106, 50)});
  expect("no card before options",
         {2, 0, 7, -1},
         {leftBorder, rightBorder, cmd(128, 46, 25), cmd(128, 106, 48)});
  expect("no card left selected",
         {2, 0, 16, -1},
         {leftBorder,
          rightBorder,
          cmd(128, 46, 25),
          cmd(128, 88, 52, 1),
          cmd(256, 88, 53),
          cmd(128, 106, 48)});
  expect("options begin dim at tick eight",
         {2, 0, 8, -1},
         {leftBorder,
          rightBorder,
          cmd(128, 46, 25),
          cmd(128, 88, 52),
          cmd(256, 88, 53),
          cmd(128, 106, 48)});
  expect("unavailable right selected blink off",
         {3, 1, 24, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 46, 26),
          cmd(128, 88, 52),
          cmd(256, 88, 53),
          cmd(128, 106, 49)});
  expect("warning",
         {4, 1, 16, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 22, 27),
          cmd(128, 38, 28),
          cmd(128, 54, 29),
          cmd(128, 70, 30),
          cmd(128, 88, 52),
          cmd(256, 88, 54, 1),
          cmd(128, 106, 49)});
  expect("format question",
         {5, 0, 16, 1},
         {leftBorder,
          rightBorder,
          cmd(128, 30, 32),
          cmd(128, 46, 33),
          cmd(128, 62, 34),
          cmd(128, 88, 55, 1),
          cmd(256, 88, 54),
          cmd(128, 106, 50)});
  expect("format warning",
         {6, 0, 16, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 30, 27),
          cmd(128, 46, 35),
          cmd(128, 62, 36),
          cmd(128, 88, 55, 1),
          cmd(256, 88, 54),
          cmd(128, 106, 49)});
  expect("formatting", {7, 0, 0, 0}, {leftBorder, rightBorder, cmd(128, 46, 37)});
  expect("format complete",
         {8, 0, 16, 0},
         {leftBorder, rightBorder, cmd(128, 46, 38), cmd(128, 88, 57, 1), cmd(128, 106, 49)});
  expect("format failed",
         {9, 0, 16, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 46, 39),
          cmd(128, 88, 53, 1),
          cmd(256, 88, 54),
          cmd(128, 106, 49)});
  expect("create confirm",
         {10, 1, 16, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 30, 31),
          cmd(128, 46, 40),
          cmd(128, 62, 41),
          cmd(128, 88, 56),
          cmd(256, 88, 54, 1),
          cmd(128, 106, 49)});
  expect("creating save",
         {11, 0, 0, 1},
         {leftBorder, rightBorder, cmd(128, 46, 42), cmd(128, 106, 50)});
  expect("card full",
         {12, 0, 16, 1},
         {leftBorder,
          rightBorder,
          cmd(128, 22, 32),
          cmd(128, 38, 43),
          cmd(128, 54, 44),
          cmd(128, 70, 63),
          cmd(128, 88, 57, 1),
          cmd(128, 106, 50)});
  expect("cannot create",
         {13, 1, 16, 0},
         {leftBorder,
          rightBorder,
          cmd(128, 46, 45),
          cmd(128, 88, 53),
          cmd(256, 88, 54, 1),
          cmd(128, 106, 49)});
  expect("missing case 14", {14, 0, 0, 0}, {leftBorder, rightBorder});
  expect("select card",
         {15, 1, 16, 0},
         {leftBorder, rightBorder, cmd(128, 46, 51), cmd(128, 88, 60), cmd(256, 88, 61, 1)});
  expect("unknown substate", {99, 0, 0, 0}, {leftBorder, rightBorder});

  expectMode2("save picker setup", {.state = 0}, {leftBorder, rightBorder});

  Mode2Input newGame = {.state = 1, .optionSelected = 1, .subTick = 0};
  newGame.slots = {Mode2Slot{},
                   {.occupied = true, .homeworldSprite = 3, .dragonCount = 12},
                   {.occupied = true, .homeworldSprite = 6, .dragonCount = 5}};
  expectMode2("select new-game slot",
              newGame,
              {leftBorder,
               rightBorder,
               cmd(140, 38, 8, 3),
               cmd(220, 38, 3),
               cmd(264, 22, 23),
               cmd(248, 22, 15),
               cmd(232, 22, 14),
               cmd(300, 38, 6, 3),
               cmd(344, 22, 23, 3),
               cmd(328, 22, 18, 3),
               cmd(220, 38, 9),
               cmd(128, 88, 59, 1),
               cmd(256, 88, 58),
               cmd(128, 106, 46)});

  Mode2Input overwrite = {.state = 2, .optionSelected = 1, .subTick = 16, .secondaryOption = 0};
  overwrite.slots = {Mode2Slot{.occupied = true, .homeworldSprite = 2, .dragonCount = 10},
                     Mode2Slot{.occupied = true, .homeworldSprite = 3, .dragonCount = 21},
                     Mode2Slot{.occupied = true, .homeworldSprite = 4, .dragonCount = 32}};
  expectMode2("confirm overwrite at capacity",
              overwrite,
              {leftBorder,
               rightBorder,
               cmd(140, 38, 2, 3),
               cmd(184, 22, 23, 3),
               cmd(168, 22, 13, 3),
               cmd(152, 22, 14, 3),
               cmd(220, 38, 3),
               cmd(264, 22, 23),
               cmd(248, 22, 14),
               cmd(232, 22, 15),
               cmd(300, 38, 4, 3),
               cmd(344, 22, 23, 3),
               cmd(328, 22, 15, 3),
               cmd(312, 22, 16, 3),
               cmd(220, 38, 10),
               cmd(220, 38, 9),
               cmd(128, 88, 62, 1),
               cmd(256, 88, 54)});

  Mode2Input loadGame = {.state = 3, .optionSelected = 2, .subTick = 8};
  expectMode2("select load slot blink off",
              loadGame,
              {leftBorder,
               rightBorder,
               cmd(140, 38, 8, 3),
               cmd(220, 38, 8, 3),
               cmd(300, 38, 8, 3),
               cmd(128, 88, 59),
               cmd(256, 88, 58, 1),
               cmd(128, 106, 47)});

  Mode2Input chooseAction = {.state = 4, .optionSelected = 1, .subTick = 0, .cardSelected = 1};
  expectMode2("choose new or load",
              chooseAction,
              {leftBorder,
               rightBorder,
               cmd(140, 38, 8, 3),
               cmd(220, 38, 8, 3),
               cmd(300, 38, 8, 3),
               cmd(128, 88, 59),
               cmd(256, 88, 58, 1),
               cmd(128, 106, 50)});

  expectMode2("save picker slide out",
              {.state = 5, .subTick = 3, .slideY = -24},
              {cmd(108, -24, 1), cmd(255, -24, -1)});

  const auto base = spyro::title_menu_recipe::buildMode1({4, 1, 16, 0});
  auto mutated = base;
  size_t mismatch = 99;
  if (!spyro::title_menu_recipe::sameCommands(base, mutated, mismatch)) {
    std::fputs("title_menu_recipe: exact oracle stream did not compare equal\n", stderr);
    ++failures;
  }
  mutated.commands[3].sprite ^= 1;
  if (spyro::title_menu_recipe::sameCommands(base, mutated, mismatch) || mismatch != 3u) {
    std::fputs("title_menu_recipe: command mutation was not rejected at index 3\n", stderr);
    ++failures;
  }
  mutated = base;
  --mutated.size;
  if (spyro::title_menu_recipe::sameCommands(base, mutated, mismatch) || mismatch != mutated.size) {
    std::fputs("title_menu_recipe: truncated stream was not rejected at its prefix length\n",
               stderr);
    ++failures;
  }

  if (failures != 0) {
    return 1;
  }
  std::puts("title_menu_recipe: PASS (26 recipes + exact/mutated/truncated oracle streams)");
  return 0;
}
