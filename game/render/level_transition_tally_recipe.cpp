#include "level_transition_tally_recipe.h"

#include "core.h"

#include <algorithm>

namespace spyro::level_transition_tally {
namespace {

// Every address here was decoded from the shipping executable with tools/re_globals.py against
// func_8001973C, so none of them is a guess.
constexpr std::uint32_t kNextLevelId = 0x800758B4u;
constexpr std::uint32_t kLevelTransTicks = 0x800756ACu;
constexpr std::uint32_t kGemTotal = 0x80075688u;
constexpr std::uint32_t kChestDuration = 0x80075740u;
constexpr std::uint32_t kLevelGemCount = 0x80077420u; // g_LevelGemCount[]
constexpr std::uint32_t kPreviousLevelIndex = 0x80075860u;
constexpr std::uint32_t kGemsBeforeEntry = 0x8007587Cu; // D_8007587C
constexpr std::uint32_t kGemsSinceEntry = 0x800756C8u;  // g_NGemsSinceLevelEntry
constexpr std::uint32_t kLevelNames = 0x8006F7F0u;      // g_LevelNames[37], an array of char*
constexpr std::uint32_t kTransGems = 0x8007571Cu;       // g_LevelTransGems, a POINTER to TransGem[]
constexpr std::uint32_t kRecentGems = 0x80077DB8u;      // g_RecentGemsCollected[32]
constexpr std::uint32_t kSine = 0x8006CBF8u;            // SINE_8's table, indexed directly
constexpr std::uint32_t kCosine = 0x8006CC78u;          // COSINE_8's table

// TransGem, include/loaders.h: two screen offsets, two targets, a live flag, an age, then a
// byte-angle triple.
constexpr std::uint32_t kTransGemSize = 0x10u;
constexpr std::uint32_t kGemXOffset = 0x00u;
constexpr std::uint32_t kGemYOffset = 0x02u;
constexpr std::uint32_t kGemActive = 0x08u;
constexpr std::uint32_t kGemRotation = 0x0Au;

constexpr std::uint32_t kMobyRotationX = 0x44u;
constexpr std::uint32_t kMobyRotationZ = 0x46u;

constexpr std::uint32_t kChestClass = 473u;
// The guest derives a gem's shade from its own class; 0x52 is where that family starts.
constexpr std::uint16_t kGemShadeBase = 0x52u;

constexpr std::int32_t kTicksEnd = 416;
constexpr std::int32_t kTreasureStart = 64;
// The tick at which the screen switches from this level's haul to the running total.
constexpr std::int32_t kTotalStart = 224;

// The caption's spacing vector and widths, as func_8001973C passes them.
constexpr hud_text::Point3 kCaptionSpacing{16, 1, 5120};
constexpr std::int32_t kCaptionSpaceWidth = 18;
constexpr std::uint8_t kCaptionShade = 2;
constexpr std::int32_t kCounterPitch = 20;

// SINE_8: the guest's 256-entry table indexed directly by a byte. Every index below is an easing
// phase the guest itself keeps under 256; the mask is that table's own domain rather than a
// tolerance, and it keeps the one int-to-index narrowing in a single place.
std::int32_t sine8(const std::array<std::int16_t, 256> &sine, std::int32_t phase) {
  return sine[(std::size_t)(phase & 0xFF)];
}

// The two easing curves. The caption rides a >> 7 sine and the treasure line a >> 6 one, so they
// are not the same motion at different amplitudes and must not share a helper that hides the shift.
std::int32_t captionY(std::int32_t ticks, const std::array<std::int16_t, 256> &sine) {
  if (ticks < 32) {
    return sine8(sine, ticks * 2) >> 7;
  }
  if (ticks >= 385) {
    return sine8(sine, (kTicksEnd - ticks) * 2) >> 7;
  }
  return 32;
}

std::int32_t treasureY(std::int32_t ticks,
                       std::int32_t chestDuration,
                       const std::array<std::int16_t, 256> &sine) {
  if (ticks < 96) {
    return 272 - (sine8(sine, (ticks - kTreasureStart) * 2) >> 6);
  }
  if (ticks < chestDuration) {
    return 208;
  }
  if (ticks < chestDuration + 32) {
    // The guest subtracts 32 from the tick, not from the duration, so the phase runs backwards
    // here.
    const std::int32_t phase = ticks - 32;
    return 272 - (sine8(sine, (chestDuration - phase) * 2) >> 6);
  }
  if (ticks <= 223) {
    return 272;
  }
  if (ticks < 256) {
    return 272 - (sine8(sine, (ticks - kTotalStart) * 2) >> 6);
  }
  if (ticks <= 383) {
    return 208;
  }
  if (ticks < kTicksEnd) {
    return 272 - (sine8(sine, (kTicksEnd - ticks) * 2) >> 6);
  }
  return 272;
}

// The count-up and count-down the tally animates. `collected` is this level's haul.
std::int32_t displayedCounter(const State &state, std::int32_t collected) {
  const auto ramp = [&](std::int32_t from) {
    std::int32_t duration = (state.gemsSinceEntry + 1) * 2;
    duration = std::min(duration, 64);
    std::int32_t countdown = from + duration - state.ticks;
    countdown = std::max(countdown, 0);
    countdown = std::min(countdown, duration);
    return (collected * countdown) / duration;
  };
  if (state.ticks < 128) {
    return collected;
  }
  if (state.ticks < kTotalStart) {
    return ramp(128);
  }
  if (state.ticks < 272) {
    return state.gemTotal - collected;
  }
  return state.gemTotal - ramp(272);
}

// The guest centres the treasure block by the width of the total, one decimal digit at a time.
std::int32_t centreOffset(std::int32_t gemTotal) {
  std::int32_t offset = 40;
  for (std::int32_t value = gemTotal; value >= 10; value /= 10) {
    offset -= 10;
  }
  return offset;
}

std::array<std::int16_t, 256> readTable(Core *core, std::uint32_t base) {
  std::array<std::int16_t, 256> table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i] = (std::int16_t)core->mem_r16s(base + (std::uint32_t)i * 2u);
  }
  return table;
}

std::string readString(Core *core, std::uint32_t address) {
  std::string out;
  for (std::uint32_t i = 0; i < 64u; ++i) {
    const std::uint8_t ch = (std::uint8_t)core->mem_r8(address + i);
    if (ch == 0u) {
      return out;
    }
    out.push_back((char)ch);
  }
  return out;
}

} // namespace

std::int32_t levelNameIndex(std::int32_t nextLevelId) {
  return (nextLevelId / 10 - 1) * 6 + (nextLevelId % 10);
}

std::string captionText(std::int32_t nextLevelId, const std::string &levelName) {
  if (nextLevelId % 10 == 0) {
    return "RETURNING HOME...";
  }
  // The boss of each of the first six homeworlds is level 4, and 63 is Gnasty Gnorc himself.
  if ((nextLevelId < 60 && nextLevelId % 10 == 4) || nextLevelId == 63) {
    return "CONFRONTING " + levelName + "...";
  }
  return "ENTERING " + levelName + "...";
}

Plan plan(const State &state) {
  const auto &sine = state.sine;
  Plan out;
  const std::string caption = captionText(state.nextLevelId, state.levelName);
  const hud_text::Point3 captionAt{
      256 - ((std::int32_t)caption.size() - 1) * 8, captionY(state.ticks, sine), 4352};
  out.caption = {hud_text::layoutCaption(caption, captionAt, kCaptionSpacing, kCaptionSpaceWidth),
                 kCaptionShade};
  if (state.ticks < kTreasureStart) {
    return out;
  }
  out.hasTreasure = true;
  const std::int32_t centre = centreOffset(state.gemTotal);
  const std::int32_t y = treasureY(state.ticks, state.chestDuration, sine);
  const bool total = state.ticks >= kTotalStart;
  const std::string treasure = total ? "TOTAL TREASURE" : "TREASURE FOUND";
  // The total caption is one glyph wider, so the guest nudges it left rather than re-centring.
  const hud_text::Point3 treasureAt{centre + 40 - (total ? 16 : 0), y, 4352};
  out.treasureCaption = {
      hud_text::layoutCaption(treasure, treasureAt, kCaptionSpacing, kCaptionSpaceWidth),
      kCaptionShade};

  const std::int32_t collected = state.previousLevelGems - state.gemsBeforeEntry;
  out.displayedCounter = displayedCounter(state, collected);
  out.counter = {hud_text::layoutCounter(
                     std::to_string(out.displayedCounter), {376 + centre, y, 3968}, kCounterPitch),
                 kCaptionShade};

  for (const Gem &gem : state.gems) {
    if (!gem.active) {
      continue;
    }
    out.gems.push_back({.mobyClass = gem.mobyClass,
                        .position = {320 + centre + gem.xOffset, 198 - gem.yOffset, 0x1000},
                        .rotX = gem.rotX,
                        .rotY = gem.rotY,
                        .rotZ = gem.rotZ,
                        .shadeIndex = (std::uint8_t)(gem.mobyClass - kGemShadeBase)});
  }
  out.chest = {.mobyClass = kChestClass,
               .position = {320 + centre, y + (total ? 12 : 8), total ? 0x600 : 0x800},
               .rotX = 6,
               .rotY = 0,
               .rotZ = 0xB0,
               .shadeIndex = 11};
  return out;
}

State read(Core *core) {
  State state;
  state.sine = readTable(core, kSine);
  state.nextLevelId = (std::int32_t)core->mem_r32(kNextLevelId);
  state.ticks = (std::int32_t)core->mem_r32(kLevelTransTicks);
  state.gemTotal = (std::int32_t)core->mem_r32(kGemTotal);
  state.chestDuration = (std::int32_t)core->mem_r32(kChestDuration);
  const std::uint32_t previousLevel = core->mem_r32(kPreviousLevelIndex);
  state.previousLevelGems = (std::int32_t)core->mem_r32(kLevelGemCount + previousLevel * 4u);
  state.gemsBeforeEntry = (std::int32_t)core->mem_r32(kGemsBeforeEntry);
  state.gemsSinceEntry = (std::int32_t)core->mem_r32(kGemsSinceEntry);
  const std::int32_t nameIndex = levelNameIndex(state.nextLevelId);
  if (nameIndex >= 0 && nameIndex < 37) {
    state.levelName = readString(core, core->mem_r32(kLevelNames + (std::uint32_t)nameIndex * 4u));
  }
  const std::uint32_t gems = core->mem_r32(kTransGems);
  for (std::size_t i = 0; i < state.gems.size(); ++i) {
    const std::uint32_t entry = gems + (std::uint32_t)i * kTransGemSize;
    Gem &gem = state.gems[i];
    gem.active = gems != 0u && core->mem_r8(entry + kGemActive) != 0u;
    if (!gem.active) {
      continue;
    }
    gem.xOffset = core->mem_r16s(entry + kGemXOffset);
    gem.yOffset = core->mem_r16s(entry + kGemYOffset);
    gem.rotX = (std::uint8_t)core->mem_r8(entry + kGemRotation + 0u);
    gem.rotY = (std::uint8_t)core->mem_r8(entry + kGemRotation + 1u);
    gem.rotZ = (std::uint8_t)core->mem_r8(entry + kGemRotation + 2u);
    gem.mobyClass = (std::uint16_t)core->mem_r8(kRecentGems + (std::uint32_t)i);
  }
  return state;
}

bool submit(Core *core) {
  const auto cosine = readTable(core, kCosine);
  const State state = read(core);
  const Plan tally = plan(state);

  // Count everything first: the guest writes the arena in one pass, so a partial write here would
  // leave half a screen behind with no way to take it back.
  std::size_t needed = tally.caption.layout.glyphs.size();
  if (tally.hasTreasure) {
    needed += tally.treasureCaption.layout.glyphs.size() + tally.counter.layout.glyphs.size() +
              tally.gems.size() + 1u;
  }
  if (!hud_text::fits(core, needed)) {
    return false;
  }

  // Every string the tally draws gets the same wobble, with the glyph index restarting at zero.
  const auto wobble = [&](const std::vector<std::uint32_t> &written) {
    for (std::size_t i = 0; i < written.size(); ++i) {
      const std::uint32_t phase = (std::uint32_t)((state.ticks * 2 + (std::int32_t)i * 12) & 0xFF);
      core->mem_w8(written[i] + kMobyRotationZ, (std::uint8_t)(std::int8_t)(cosine[phase] >> 7));
    }
  };
  const auto sprite = [&](const Sprite &moby) {
    const hud_text::Layout single{{{moby.position, moby.mobyClass}}, moby.position};
    const auto written = hud_text::append(core, single, moby.shadeIndex);
    if (written.empty()) {
      return;
    }
    core->mem_w8(written[0] + kMobyRotationX + 0u, moby.rotX);
    core->mem_w8(written[0] + kMobyRotationX + 1u, moby.rotY);
    core->mem_w8(written[0] + kMobyRotationX + 2u, moby.rotZ);
  };

  wobble(hud_text::append(core, tally.caption.layout, tally.caption.shadeIndex));
  if (!tally.hasTreasure) {
    return true;
  }
  wobble(hud_text::append(core, tally.treasureCaption.layout, tally.treasureCaption.shadeIndex));
  wobble(hud_text::append(core, tally.counter.layout, tally.counter.shadeIndex));
  for (const Sprite &gem : tally.gems) {
    sprite(gem);
  }
  sprite(tally.chest);
  return true;
}

} // namespace spyro::level_transition_tally
