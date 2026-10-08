#include "title_logo.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"

#include <lucent/log.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace spyro {
namespace {

// PSX VRAM is 1024x512 texels and every address here wraps exactly as the hardware does, so a CLUT
// or sprite page that runs off an edge continues on the far side.
constexpr int kVramWidth = 1024;
constexpr int kVramHeight = 512;

// Bounds a misread sprite record so it cannot ask for a snapshot the size of VRAM.
constexpr int kLogoMaximumPixels = 1 << 20;

// Shift-and-fill, not a multiply: a multiply puts 31 at 248 and leaves a wordmark's white grey.
constexpr std::uint8_t expand5(std::uint32_t five) {
  const std::uint32_t value = five & 31u;
  return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

// One 8-byte sprite record: tpage, clut, w, h, u, v. The guest's emitter loads the two halfwords
// from the start and the four bytes from the third word (game/render/scene/title_menu.cpp,
// SpriteRec::read); Spyro 1's first two records agree — 0x8006FACC holds 0x7FE00098.
struct SpriteRecord {
  std::uint16_t tpage = 0;
  std::uint16_t clut = 0;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  std::uint32_t u = 0;
  std::uint32_t v = 0;
};

SpriteRecord readSpriteRecord(Core &core, std::uint32_t address) {
  return SpriteRecord{core.mem_r16(address),
                      core.mem_r16(address + 2),
                      core.mem_r8(address + 4),
                      core.mem_r8(address + 5),
                      core.mem_r8(address + 6),
                      core.mem_r8(address + 7)};
}

// One texel of VRAM. `vram(x, y)` already wraps both axes the way the hardware does.
std::uint16_t vramTexel(GpuState &gpu, int x, int y) {
  return *gpu.vram(x, y);
}

// 8-bit VRAM packs two texels per word, even column in the low byte, so an 8-bit region read as
// 15-bit looks like noise whose neighbours agree.
std::uint8_t vramByteTexel(GpuState &gpu, int x, int y) {
  const std::uint16_t word = vramTexel(gpu, x & ~1, y);
  return (x & 1) != 0 ? static_cast<std::uint8_t>(word >> 8)
                      : static_cast<std::uint8_t>(word & 0xFFu);
}

// `entries` texels along ONE VRAM row from the CLUT word: 16 for a 4-bit page, 256 for an 8-bit
// one. Not a 16x16 block — that fetches colours the palette is not in.
void readPalette(GpuState &gpu,
                 std::uint16_t clut,
                 int entries,
                 std::vector<std::uint16_t> &palette) {
  const int baseX = paletteX(clut);
  const int y = paletteY(clut);
  palette.resize(static_cast<std::size_t>(entries));
  for (int index = 0; index < entries; ++index) {
    palette[static_cast<std::size_t>(index)] = vramTexel(gpu, baseX + index, y);
  }
}

} // namespace

// The guest keeps a pointer global; the overlay's state block hangs off it and its tick is what
// retail tests to decide its logo is up. A null pointer means the title has not reached its
// overlay, which is not a failure.
bool titleLogoGateOpen(Core &core, const TitleLogoFacts &facts) {
  if (!facts.valid || facts.gate.pointerGlobal == 0) {
    return false;
  }
  const std::uint32_t block = core.mem_r32(facts.gate.pointerGlobal);
  if (block == 0) {
    return false;
  }
  return core.mem_r32(block + facts.gate.tickOffset) >= facts.gate.tickValue;
}

std::optional<psx::host::LogoImage> extractPanelLogo(Core &core, const TitleLogoFacts &facts) {
  if (!facts.valid) {
    return std::nullopt;
  }
  if (!titleLogoGateOpen(core, facts)) {
    // The normal answer for all but the moment this title's screen is up; the caller asks again.
    return std::nullopt;
  }
  if (core.game == nullptr || facts.parts.empty() || facts.parts.size() > kTitleLogoMaximumParts) {
    lucent::warn(
        "picker", "the title's logo names {} part(s) — the panel shows none", facts.parts.size());
    return std::nullopt;
  }

  GpuState &gpu = core.game->gpu;
  // Each part's (u, v) is its recorded position on the page the game uploaded, so the wordmark is
  // placed where the game placed it and is never re-lettered as equal pieces.
  std::vector<SpriteRecord> parts;
  parts.reserve(facts.parts.size());
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  for (const TitleLogoPart &part : facts.parts) {
    SpriteRecord sprite = readSpriteRecord(core, part.record);
    if (part.clipWidth != 0 && part.clipWidth < sprite.w) {
      // The rest of this record's rectangle belongs to another sprite sharing the page.
      sprite.w = part.clipWidth;
    }
    if (sprite.w == 0 || sprite.h == 0) {
      lucent::debug("picker",
                    "the logo's sprite record at 0x{:X} is empty — the panel shows no logo",
                    part.record);
      return std::nullopt;
    }
    const int x = static_cast<int>(sprite.u);
    const int y = static_cast<int>(sprite.v);
    if (parts.empty()) {
      left = x;
      top = y;
    }
    left = std::min(left, x);
    top = std::min(top, y);
    right = std::max(right, x + static_cast<int>(sprite.w));
    bottom = std::max(bottom, y + static_cast<int>(sprite.h));
    parts.push_back(sprite);
  }
  const int width = right - left;
  const int height = bottom - top;
  if (width <= 0 || height <= 0 || width * height > kLogoMaximumPixels) {
    lucent::error("picker",
                  "the logo's parts cover {}x{} — not a wordmark — refusing to snapshot them",
                  width,
                  height);
    return std::nullopt;
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
  std::size_t visible = 0;
  for (const SpriteRecord &sprite : parts) {
    const int pageX = texturePageX(sprite.tpage);
    const int pageY = texturePageY(sprite.tpage);
    const int depth = texturePageDepth(sprite.tpage);
    std::vector<std::uint16_t> palette;
    if (depth == 0) {
      readPalette(gpu, sprite.clut, 16, palette);
    } else if (depth == 1) {
      readPalette(gpu, sprite.clut, 256, palette);
    } else if (depth != 2) {
      lucent::error("picker",
                    "the logo's texture page {:04X} names depth {} — no such format — "
                    "the panel shows no logo",
                    sprite.tpage,
                    depth);
      return std::nullopt;
    }
    for (std::uint32_t row = 0; row < sprite.h; ++row) {
      for (std::uint32_t col = 0; col < sprite.w; ++col) {
        const int x = static_cast<int>(sprite.u) + static_cast<int>(col) - left;
        const int y = static_cast<int>(sprite.v) + static_cast<int>(row) - top;
        if (x < 0 || x >= width || y < 0 || y >= height) {
          continue;
        }
        const std::size_t at = static_cast<std::size_t>(y) * width + x;
        std::uint8_t *pixel = &rgba[at * 4];
        const int texelX = pageX + static_cast<int>(sprite.u) + static_cast<int>(col);
        const int texelY = pageY + static_cast<int>(sprite.v) + static_cast<int>(row);
        std::uint32_t colour = 0;
        if (depth == 0) {
          // 4-bit: eight texels per 16-bit word, low nibble first.
          const std::uint16_t word = vramTexel(gpu, texelX & ~7, texelY);
          colour = palette[(word >> ((texelX & 7) * 4)) & 0xF];
        } else if (depth == 1) {
          colour = palette[vramByteTexel(gpu, texelX, texelY)];
        } else {
          colour = vramTexel(gpu, texelX, texelY);
        }
        // The PSX's rule: a 0x0000 texel is not drawn. Each wordmark sits on an untouched page, so
        // index 0 is the surround and must let the panel through.
        if (colour == 0) {
          pixel[3] = 0;
          continue;
        }
        pixel[0] = expand5(colour & 31u);
        pixel[1] = expand5((colour >> 5) & 31u);
        pixel[2] = expand5((colour >> 10) & 31u);
        pixel[3] = 255;
        ++visible;
      }
    }
  }

  // VRAM is readable whatever is in it, so only the picture can tell a wordmark from a rectangle
  // of nothing — and an empty panel would be cached for its whole life.
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  if (visible * 50 < pixels) {
    lucent::debug("picker",
                  "the logo's snapshot holds no artwork ({} of {} texels) — the panel shows none",
                  visible,
                  pixels);
    return std::nullopt;
  }
  lucent::info("picker",
               "the title's own logo snapshotted from its title screen: {}x{} over {} part(s), "
               "first part tpage {:04X} clut {:04X}, index 0 transparent ({} of {} texels)",
               width,
               height,
               parts.size(),
               parts.front().tpage,
               parts.front().clut,
               visible,
               pixels);
  return psx::host::LogoImage{width, height, std::move(rgba)};
}

} // namespace spyro