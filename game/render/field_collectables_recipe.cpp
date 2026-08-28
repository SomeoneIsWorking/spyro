#include "field_collectables_recipe.h"

namespace spyro::field_collectables_recipe {
namespace {

constexpr uint32_t kHud = 0x80077fa8u;
constexpr uint32_t kHudMobys = kHud + 0x44u;
constexpr uint32_t kMobySize = 0x58u;

void appendMoby(Recipe &recipe, uint32_t index) {
  recipe.shadedMobys[recipe.shadedCount++] = kHudMobys + index * kMobySize;
}

} // namespace

Recipe derive(const State &state) {
  Recipe recipe{};
  if (!state.flightLevel) {
    if (state.gemDisplay == 4u) {
      recipe.status = Status::CompletedGemTextUnowned;
      return recipe;
    }
    if (state.gemDisplay != 0u) {
      for (uint32_t i = 0; i < 5u; ++i) {
        appendMoby(recipe, i);
      }
    }
    if (state.dragonDisplay != 0u) {
      for (uint32_t i = 5u; i < 8u; ++i) {
        appendMoby(recipe, i);
      }
    }
    if (state.lifeDisplay != 0u) {
      for (uint32_t i = 8u; i < 11u; ++i) {
        appendMoby(recipe, i);
      }
    }
    if (state.keyDisplay != 0u && state.keyFlag == 1u) {
      appendMoby(recipe, 11u);
    }
  }

  const int32_t lifeCount = state.lifeDisplay != 0u ? state.lifeOrbCount : 0;
  const int32_t eggCount = state.eggDisplay != 0u ? state.eggCount : 0;
  if (lifeCount > 20 || eggCount > 12 ||
      (lifeCount > 0 ? (uint32_t)lifeCount : 0u) + (eggCount > 0 ? (uint32_t)eggCount : 0u) >
          kMaxSprites) {
    recipe.status = Status::InvalidCount;
    return recipe;
  }
  if (state.lifeDisplay != 0u) {
    for (int32_t i = 0; i < lifeCount; ++i) {
      const uint32_t index = (uint32_t)i;
      const uint32_t phase = (state.specularTime - index * 256u / 20u) & 0xffu;
      const uint8_t colour = (uint8_t)(((int32_t)state.cosine[phase] >> 7) + 128);
      recipe.sprites[recipe.spriteCount++] = {.rect = state.rects[12u + index],
                                              .tile = state.tiles[0],
                                              .r = colour,
                                              .g = colour,
                                              .b = colour};
    }
  }
  if (state.eggDisplay != 0u) {
    for (int32_t i = 0; i < eggCount; ++i) {
      const uint32_t index = (uint32_t)i;
      const uint32_t tile = (state.eggPhase + index) % 9u + 1u;
      recipe.sprites[recipe.spriteCount++] = {.rect = state.rects[index],
                                              .tile = state.tiles[tile],
                                              .r = 0x80u,
                                              .g = 0x80u,
                                              .b = 0x80u};
    }
  }
  return recipe;
}

} // namespace spyro::field_collectables_recipe
