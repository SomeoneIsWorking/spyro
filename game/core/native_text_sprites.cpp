// Native ownership of BuildTextSprites (0x800181AC).
//
// Ground truth is SCUS_942.28 0x800181AC..0x8001844B (168 instructions). The executable,
// Ghidra output, open-spyro's length-exact WIP body, and the byte-matching spyro-1 decomp agree on
// the glyph-record layout and capital/spacing state machine. The only children are FillWord
// 0x80016914 and CopyVector 0x80017700, both already owned and deliberately kept as dispatch
// boundaries. The generated parent remains compiled as the per-call differential oracle.
#include "core.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"
#include "text_sprites.h"

namespace {

constexpr uint32_t kSpriteCursor = 0x80075710u;
constexpr uint32_t kSpriteBytes = 0x58u;
constexpr uint32_t kPositionOffset = 0x0Cu;
constexpr uint32_t kClassOffset = 0x36u;
constexpr uint32_t kDepthOffset = 0x47u;
constexpr uint32_t kShadeOffset = 0x4Fu;
constexpr uint32_t kRadiusOffset = 0x50u;

void fillSpriteRecord(Core *c) {
  c->r[4] = 0x80070000u;
  gte_hold_src(c, 4, kSpriteCursor);
  c->r[4] = c->mem_r32(kSpriteCursor) - kSpriteBytes;
  c->r[1] = 0x80070000u;
  c->mem_w32(kSpriteCursor, c->r[4]);
  gte_copy_pz(c, 4, kSpriteCursor);
  c->r[31] = 0x8001821Cu;
  c->r[6] = kSpriteBytes;
  func_80016914(c);
}

void copyPenPosition(Core *c) {
  c->r[4] = c->mem_r32(kSpriteCursor);
  c->r[5] = c->r[16];
  c->r[31] = 0x80018230u;
  c->r[4] += kPositionOffset;
  func_80017700(c);
}

void writeGlyphClass(Core *c, spyro::TextGlyphKind kind) {
  const uint8_t character = static_cast<uint8_t>(c->r[4]);
  const uint16_t glyphClass = spyro::textGlyphClass(kind, character);

  // Keep the executable's scratch-register and HI/LO effects, not just the visible record.
  switch (kind) {
  case spyro::TextGlyphKind::Digit:
    c->r[2] = glyphClass;
    c->r[3] = c->mem_r32(kSpriteCursor);
    c->mem_w16(c->r[3] + kClassOffset, glyphClass);
    return;
  case spyro::TextGlyphKind::Letter:
    c->r[2] = glyphClass;
    c->r[3] = c->mem_r32(kSpriteCursor);
    c->mem_w16(c->r[3] + kClassOffset, glyphClass);
    return;
  case spyro::TextGlyphKind::Exclamation:
  case spyro::TextGlyphKind::Comma:
  case spyro::TextGlyphKind::Question:
  case spyro::TextGlyphKind::Period:
    c->r[3] = c->mem_r32(kSpriteCursor);
    c->r[2] = glyphClass;
    c->mem_w16(c->r[3] + kClassOffset, glyphClass);
    return;
  case spyro::TextGlyphKind::Fallback:
    break;
  }

  c->r[3] = 0x55550000u;
  c->r[4] = c->mem_r32(kSpriteCursor);
  c->r[2] = glyphClass;
  c->mem_w16(c->r[4] + kClassOffset, glyphClass);
  c->r[2] = c->mem_r32(c->r[19]);
  c->r[3] |= 0x5556u;
  c->r[2] <<= 1;
  const int64_t product = static_cast<int64_t>(static_cast<int32_t>(c->r[2])) *
                          static_cast<int64_t>(static_cast<int32_t>(c->r[3]));
  c->lo = static_cast<uint32_t>(product);
  c->hi = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32);
  c->r[2] = static_cast<uint32_t>(static_cast<int32_t>(c->r[2]) >> 31);
  c->r[3] = c->mem_r32(c->r[4] + 16u);
  c->r[8] = c->hi;
  c->r[2] = c->r[8] - c->r[2];
  c->r[3] -= c->r[2];
  c->mem_w32(c->r[4] + 16u, c->r[3]);
}

void writeGlyphMetadataAndAdvancePen(Core *c, spyro::TextGlyphKind kind) {
  c->r[3] = c->mem_r32(kSpriteCursor);
  c->r[2] = 127u;
  c->mem_w8(c->r[3] + kDepthOffset, static_cast<uint8_t>(c->r[2]));
  c->r[2] = c->mem_r32(kSpriteCursor);
  c->mem_w8(c->r[2] + kShadeOffset, static_cast<uint8_t>(c->r[21]));
  c->r[3] = c->mem_r32(kSpriteCursor);
  c->r[2] = 255u;
  c->mem_w8(c->r[3] + kRadiusOffset, static_cast<uint8_t>(c->r[2]));

  c->r[2] = c->mem_r32(c->r[16]);
  if (c->r[18] != 0u) {
    c->r[2] += c->r[20];
  } else {
    c->r[3] = c->mem_r32(c->r[19]);
    c->r[2] += c->r[3];
  }
  c->mem_w32(c->r[16], c->r[2]);
  c->r[2] = c->mem_r8(c->r[17]) - 48u;
  c->r[18] = static_cast<uint32_t>(spyro::textGlyphLeavesCapital(kind));
}

void advanceSpace(Core *c) {
  c->r[3] = c->mem_r32(c->r[19]);
  c->r[2] = c->r[3] << 1;
  c->r[3] = c->r[2] + c->r[3];
  c->r[18] = 1u;
  if (static_cast<int32_t>(c->r[3]) < 0) {
    c->r[3] += 3u;
  }
  c->r[2] = c->mem_r32(c->r[16]);
  c->r[3] = static_cast<uint32_t>(static_cast<int32_t>(c->r[3]) >> 2);
  c->r[2] += c->r[3];
  c->mem_w32(c->r[16], c->r[2]);
}

void buildTextSpritesNative(Core *c) {
  c->r[29] -= 48u;
  c->mem_w32(c->r[29] + 20u, c->r[17]);
  c->r[17] = c->r[4];
  c->mem_w32(c->r[29] + 16u, c->r[16]);
  c->r[16] = c->r[5];
  c->mem_w32(c->r[29] + 28u, c->r[19]);
  c->r[19] = c->r[6];
  c->mem_w32(c->r[29] + 32u, c->r[20]);
  c->r[20] = c->r[7];
  c->mem_w32(c->r[29] + 24u, c->r[18]);
  c->mem_w32(c->r[29] + 40u, c->r[31]);
  c->mem_w32(c->r[29] + 36u, c->r[21]);
  c->r[3] = c->mem_r8(c->r[17]);
  c->r[21] = c->mem_r32(c->r[29] + 64u);
  c->r[18] = 1u;

  while (c->r[3] != 0u) {
    if (c->pending_work) {
      rec_irq_poll(c);
    }
    c->r[3] &= 255u;
    c->r[2] = 32u;
    c->r[5] = 0u;
    if (c->r[3] == c->r[2]) {
      advanceSpace(c);
    } else {
      fillSpriteRecord(c);
      copyPenPosition(c);

      c->r[3] = c->mem_r8(c->r[17]);
      c->r[2] = 33u;
      const auto kind = spyro::classifyTextGlyph(static_cast<uint8_t>(c->r[3]));
      c->r[2] = 63u;
      if (spyro::textGlyphForcesCapital(kind)) {
        c->r[18] = 1u;
      }

      if (c->r[18] == 0u) {
        c->r[4] = c->mem_r32(kSpriteCursor);
        c->r[3] = c->mem_r32(c->r[19] + 4u);
        c->r[2] = c->mem_r32(c->r[4] + 16u);
        c->r[2] += c->r[3];
        c->mem_w32(c->r[4] + 16u, c->r[2]);
        gte_hold_src(c, 2, c->r[19] + 8u);
        c->r[2] = c->mem_r32(c->r[19] + 8u);
        c->mem_w32(c->r[4] + 20u, c->r[2]);
        gte_copy_pz(c, 2, c->r[4] + 20u);
      }

      c->r[4] = c->mem_r8(c->r[17]);
      c->r[2] = c->r[4] - 48u;
      c->r[2] = static_cast<uint32_t>(c->r[2] < 10u);
      c->r[2] = c->r[4] + 212u;
      if (kind != spyro::TextGlyphKind::Digit) {
        c->r[2] = c->r[4] - 65u;
        c->r[2] = static_cast<uint32_t>(c->r[2] < 26u);
        c->r[2] = c->r[4] + 361u;
      }
      if (kind != spyro::TextGlyphKind::Digit && kind != spyro::TextGlyphKind::Letter) {
        c->r[3] = c->r[4] & 255u;
      }
      writeGlyphClass(c, kind);
      writeGlyphMetadataAndAdvancePen(c, kind);
    }

    ++c->r[17];
    c->r[3] = c->mem_r8(c->r[17]);
  }

  c->r[2] = c->mem_r32(kSpriteCursor);
  c->r[31] = c->mem_r32(c->r[29] + 40u);
  c->r[21] = c->mem_r32(c->r[29] + 36u);
  c->r[20] = c->mem_r32(c->r[29] + 32u);
  c->r[19] = c->mem_r32(c->r[29] + 28u);
  c->r[18] = c->mem_r32(c->r[29] + 24u);
  c->r[17] = c->mem_r32(c->r[29] + 20u);
  c->r[16] = c->mem_r32(c->r[29] + 16u);
  c->r[29] += 48u;
}

void buildTextSpritesOwned(Core *c) {
  ndiff_run(c, "text-sprites@0x800181AC", buildTextSpritesNative, gen_func_800181AC);
}

} // namespace

void spyro_register_native_text_sprites() {
  psxport_recomp()->shard_set_override(0x800181ACu, buildTextSpritesOwned);
}
