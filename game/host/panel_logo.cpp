#include "panel_logo.h"

#include "core.h"
#include "game.h"
#include "gpu_native_internal.h"

#include <lucent/log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace spyro {
namespace {

// The PSX's texture memory is 1024x512 texels, and every address here wraps exactly as the hardware
// does: a CLUT whose top row is the last row of VRAM continues on the first, and a sprite whose
// page runs off the right edge continues on the left. Wrapping is not a shortcut here, it is what
// the hardware the guest wrote for does.
constexpr int kVramWidth = 1024;
constexpr int kVramHeight = 512;

// A logo is small. This is the largest rectangle that could possibly be one title's wordmark, and
// it exists so a misread sprite record cannot ask for a snapshot the size of VRAM.
constexpr int kLogoMaximumPixels = 1 << 20;

// How one PSX 1555 halfword becomes a host channel. The PSX stores 5 bits per channel; the host
// wants 8. This is the shift-and-fill that maps the full range onto the full range — a straight
// multiply would put 31 at 248 and leave the white of a wordmark grey.
constexpr std::uint8_t expand5(std::uint32_t five) {
  const std::uint32_t value = five & 31u;
  return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

// ONE 8-BYTE SPRITE RECORD, AS THE GAME KEEPS IT: tpage, clut, w, h, u, v — in that order. The
// order is not a guess: the guest's emitter loads the two halfwords from the record's start and the
// four bytes from its third word (game/render/scene/title_menu.cpp, whose SpriteRec::read is the
// port of those loads), and Spyro 1's own table agrees at its first two records — 0x8006FACC holds
// 0x7FE00098, which is tpage 0x0098 (8-bit indexed), clut 0x7FE0, 255x128 at (0,0).
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

// One 8-BIT INDEXED texel. 8-bit VRAM packs two texels into every 16-bit word — the even column in
// the low byte — which is why an 8-bit region read as 15-bit looks like noise whose neighbours
// agree, and it is why the logo's own record names depth 1 rather than 2.
std::uint8_t vramByteTexel(GpuState &gpu, int x, int y) {
  const std::uint16_t word = vramTexel(gpu, x & ~1, y);
  return (x & 1) != 0 ? static_cast<std::uint8_t>(word >> 8)
                      : static_cast<std::uint8_t>(word & 0xFFu);
}

// THE PALETTE A CLUT WORD NAMES, read exactly as the emulator this port executes the guest through
// reads it (vendor/beetle-psx/mednafen/psx/gpu_common.h, DEFINE_Update_CLUT_Cache): `y = (clut >>
// 6) & 0x1FF`, `cxo = (clut & 0x3F) << 4`, and then `count` texels along that ONE row — 16 for a
// 4-bit page, 256 for an 8-bit one. Not a 16x16 block: an 8-bit CLUT is 256 consecutive texels in a
// single VRAM row, which is also what the renderer registers as the CLUT's footprint
// (GpuState::set_clut's vram_register_sampled(..., 256, 1, "clut")). Reading it as a block is what
// produced per-texel colour noise inside a correct silhouette — the silhouette from the indices,
// the noise from colours past the sixteenth entry being fetched from rows the palette is not in.
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

PanelLogo::PanelLogo(int width, int height, std::vector<std::uint8_t> rgba)
    : m_width(width), m_height(height), m_rgba(std::move(rgba)) {}

const PanelLogo &PanelLogo::magnifiedNearest(int factor) const {
  if (factor <= 1 || m_width <= 0 || m_height <= 0) {
    return *this;
  }
  if (m_magnified != nullptr && m_magnifiedFactor == factor) {
    return *m_magnified;
  }
  // NEAREST NEIGHBOUR, EXPLICITLY: each source texel becomes a `factor` x `factor` block of itself.
  // A block is the whole point — every texel of the wordmark is the same number of window pixels,
  // so the logo keeps the game's own pixel grid instead of turning into an interpolated picture.
  std::vector<std::uint8_t> scaled(static_cast<std::size_t>(m_width) * factor *
                                       static_cast<std::size_t>(m_height) * factor * 4,
                                   0);
  const int outW = m_width * factor;
  const int outH = m_height * factor;
  for (int y = 0; y < outH; ++y) {
    const int sy = y / factor;
    for (int x = 0; x < outW; ++x) {
      const int sx = x / factor;
      const std::size_t from = (static_cast<std::size_t>(sy) * m_width + sx) * 4;
      const std::size_t to = (static_cast<std::size_t>(y) * outW + x) * 4;
      std::memcpy(&scaled[to], &m_rgba[from], 4);
    }
  }
  m_magnified = std::make_unique<PanelLogo>(outW, outH, std::move(scaled));
  m_magnifiedFactor = factor;
  return *m_magnified;
}

PanelLogo::~PanelLogo() {
  if (m_texture != nullptr) {
    // The texture belongs to the presentation this object was built against, which outlives every
    // panel by the picker's own contract, so releasing it here is safe — and is what keeps a picker
    // that is rebuilt (leaving to a title and coming back) from leaking one texture per title.
    SDL_ReleaseGPUTexture(m_textureDevice, m_texture);
  }
}

SDL_GPUTexture *PanelLogo::texture(GpuDevice &device) const {
  if (empty()) {
    return nullptr;
  }
  // One texture per DEVICE: a picker rebuilt against the same presentation must not re-upload, and
  // one whose presentation changed must not draw through a texture that device does not own.
  if (m_texture != nullptr && m_textureDevice == device.s_dev) {
    return m_texture;
  }
  if (m_texture != nullptr) {
    SDL_ReleaseGPUTexture(m_textureDevice, m_texture);
    m_texture = nullptr;
  }
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.width = static_cast<Uint32>(m_width);
  info.height = static_cast<Uint32>(m_height);
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  // Sampler-only, and the pixels arrive through a TRANSFER BUFFER rather than a copy pass. This SDL
  // has no COPY_DST texture usage flag, so a copy pass into a plain texture is dropped by the
  // driver and the texture stays whatever the allocator gave it — which is to say noise, drawn over
  // a title's own logo. The transfer path is what this codebase uses everywhere else.
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device.s_dev, &info);
  if (texture == nullptr) {
    lucent::error("picker", "the logo's {}x{} texture could not be created", m_width, m_height);
    return nullptr;
  }
  SDL_GPUTransferBufferCreateInfo upload = {};
  upload.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  upload.size = m_rgba.size();
  SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(device.s_dev, &upload);
  if (xfer == nullptr) {
    SDL_ReleaseGPUTexture(device.s_dev, texture);
    lucent::error(
        "picker", "the logo's {}x{} upload buffer could not be created", m_width, m_height);
    return nullptr;
  }
  void *mapped = SDL_MapGPUTransferBuffer(device.s_dev, xfer, false);
  if (mapped == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device.s_dev, xfer);
    SDL_ReleaseGPUTexture(device.s_dev, texture);
    lucent::error("picker", "the logo's {}x{} pixels could not be mapped", m_width, m_height);
    return nullptr;
  }
  SDL_memcpy(mapped, m_rgba.data(), m_rgba.size());
  SDL_UnmapGPUTransferBuffer(device.s_dev, xfer);
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device.s_dev);
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion region = {};
  region.texture = texture;
  region.w = static_cast<Uint32>(m_width);
  region.h = static_cast<Uint32>(m_height);
  region.d = 1;
  SDL_GPUTextureTransferInfo from = {};
  from.transfer_buffer = xfer;
  from.pixels_per_row = static_cast<Uint32>(m_width);
  from.rows_per_layer = static_cast<Uint32>(m_height);
  SDL_UploadToGPUTexture(copy, &from, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device.s_dev, xfer);
  m_texture = texture;
  m_textureDevice = device.s_dev;
  return m_texture;
}

// THE TITLE'S OWN GATE. The guest keeps a pointer global; the overlay's state block hangs off it
// and its tick is what retail itself tests to decide its logo is up. A gate whose pointer is still
// null is a title that has not reached its overlay, which is not a failure and is not logged as
// one.
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

std::unique_ptr<PanelLogo> extractPanelLogo(Core &core, const TitleLogoFacts &facts) {
  if (!facts.valid) {
    return nullptr;
  }
  if (!titleLogoGateOpen(core, facts)) {
    // The normal answer for the whole of a panel's life except the moment its own title screen is
    // up. The panel asks again on the next step, so this says nothing to the player and is not
    // worth a log line.
    return nullptr;
  }
  if (core.game == nullptr || facts.parts.empty() || facts.parts.size() > kTitleLogoMaximumParts) {
    lucent::warn(
        "picker", "the title's logo names {} part(s) — the panel shows none", facts.parts.size());
    return nullptr;
  }

  GpuState &gpu = core.game->gpu;
  // The parts are the wordmark's own sprites, each one naming where it sits on the page the game
  // uploaded: their (u, v) IS the recorded relative position, so the wordmark is composed by
  // placing each part where the game placed it rather than by lining the parts up side by side,
  // which would quietly re-letter a wordmark that is not made of equal pieces.
  std::vector<SpriteRecord> parts;
  parts.reserve(facts.parts.size());
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  for (const TitleLogoPart &part : facts.parts) {
    SpriteRecord sprite = readSpriteRecord(core, part.record);
    if (part.clipWidth != 0 && part.clipWidth < sprite.w) {
      // The rest of this record's rectangle belongs to another sprite that shares the page; keeping
      // it would composite the menu's banner into the wordmark.
      sprite.w = part.clipWidth;
    }
    if (sprite.w == 0 || sprite.h == 0) {
      lucent::debug("picker",
                    "the logo's sprite record at 0x{:X} is empty — the panel shows no logo",
                    part.record);
      return nullptr;
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
    return nullptr;
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
      return nullptr;
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
          // 4-bit: eight texels per 16-bit word, the low nibble first, so this texel's word is the
          // one its group of eight shares.
          const std::uint16_t word = vramTexel(gpu, texelX & ~7, texelY);
          colour = palette[(word >> ((texelX & 7) * 4)) & 0xF];
        } else if (depth == 1) {
          colour = palette[vramByteTexel(gpu, texelX, texelY)];
        } else {
          colour = vramTexel(gpu, texelX, texelY);
        }
        // THE PSX'S TRANSPARENCY RULE, EXACTLY: index 0 is colour 0x0000, and a 0x0000 texel is not
        // drawn. Every one of these wordmarks is drawn on an untouched page, so index 0 is the
        // surround and must let the panel through — otherwise the logo arrives as a black
        // rectangle, which on a dark panel is a logo nobody can see.
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

  // WHAT IS LEFT HAS TO LOOK LIKE A LOGO. The snapshot itself always succeeds — VRAM is readable
  // whatever is in it — so a rectangle of nothing would otherwise be cached for the life of the
  // panel, and an empty panel reads as a broken one. Only the picture can answer that.
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  if (visible * 50 < pixels) {
    lucent::debug("picker",
                  "the logo's snapshot holds no artwork ({} of {} texels) — the panel shows none",
                  visible,
                  pixels);
    return nullptr;
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
  return std::make_unique<PanelLogo>(width, height, std::move(rgba));
}

} // namespace spyro