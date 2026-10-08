#include "stage13_text_run.h"

#include "core.h"
#include "guest_actor_pool.h"

#include <cstdint>

namespace {

// The caption strings the guest itself lays out, by guest address.
constexpr uint32_t kNewGameCaption = 0x80010CF0u;
constexpr uint32_t kContinuePromptCaption = 0x80010D28u;
constexpr uint32_t kContinueAnsweredCaption = 0x80010D0Cu;
constexpr uint32_t kTallyCaption = 0x80010D40u;

using spyro::guest_actor_pool::kCursorAddress;
using spyro::guest_actor_pool::kRecordSize;

void zero_actor(Core *c, uint32_t actor) {
  for (uint32_t o = 0; o < kRecordSize; o += 4u) {
    c->mem_w32(actor + o, 0);
  }
}

void build_text(Core *c,
                uint32_t string,
                int32_t &x,
                const int32_t pos[3],
                const int32_t scale[3],
                int32_t digit_advance,
                uint8_t style) {
  bool previous_digit_or_punct = true;
  for (uint32_t p = string; c->mem_r8(p); ++p) {
    const uint8_t ch = c->mem_r8(p);
    if (ch == 0x20u) {
      x += (scale[0] * 3) / 4;
      previous_digit_or_punct = true;
      continue;
    }
    uint32_t actor = c->mem_r32(kCursorAddress) - kRecordSize;
    c->mem_w32(kCursorAddress, actor);
    zero_actor(c, actor);
    c->mem_w32(actor + 0x0Cu, (uint32_t)x);
    c->mem_w32(actor + 0x10u, (uint32_t)pos[1]);
    c->mem_w32(actor + 0x14u, (uint32_t)pos[2]);
    if (ch == 0x21u || ch == 0x3Fu) {
      previous_digit_or_punct = true;
    }
    if (!previous_digit_or_punct) {
      c->mem_w32(actor + 0x10u, (uint32_t)((int32_t)c->mem_r32(actor + 0x10u) + scale[1]));
      c->mem_w32(actor + 0x14u, (uint32_t)scale[2]);
    }
    uint16_t mesh = 0x4Cu;
    if (ch >= '0' && ch <= '9') {
      mesh = (uint16_t)(ch + 0xD4u);
    } else if (ch >= 'A' && ch <= 'Z') {
      mesh = (uint16_t)(ch + 0x169u);
    } else if (ch == '!') {
      mesh = 0x4Bu;
    } else if (ch == '?') {
      mesh = 0x116u;
    } else if (ch == '.') {
      mesh = 0x147u;
    } else if (ch != ',') {
      c->mem_w32(actor + 0x10u,
                 (uint32_t)((int32_t)c->mem_r32(actor + 0x10u) - (scale[0] * 2) / 3));
    }
    c->mem_w16(actor + 0x36u, mesh);
    c->mem_w8(actor + 0x47u, 0x7Fu);
    c->mem_w8(actor + 0x4Fu, style);
    c->mem_w8(actor + 0x50u, 0xFFu);
    x += previous_digit_or_punct ? digit_advance : scale[0];
    previous_digit_or_punct = ch >= '0' && ch <= '9';
  }
}

} // namespace

bool spyro::stage13_text_run::present(Core *c, uint32_t originalPool) {
  const uint32_t state = c->mem_r32(spyro::stage13_text_run::kStateAddress);
  const int32_t timer = (int32_t)c->mem_r32(spyro::stage13_text_run::kTimerAddress);
  if (state != 2u || timer <= 0x8B) {
    return false;
  }
  int32_t x = 0;
  const int32_t pos[3] = {0, 0x78, 0x1400};
  const int32_t scale[3] = {0x0E, 1, 0x1600};
  int32_t stop = 0;
  const uint32_t text = c->mem_r32(spyro::stage13_text_run::kTextAddress);
  if (text == 0u) {
    x = 0x5C;
    build_text(c, kNewGameCaption, x, pos, scale, 0x10, 0x0B);
    stop = 0xB8;
  } else if (text == 1u && c->mem_r8(spyro::stage13_text_run::kContinueFlagAddress) == 0u) {
    x = 100;
    build_text(c, kContinuePromptCaption, x, pos, scale, 0x10, 0x0B);
    stop = 0xB6;
  } else if (text == 1u) {
    x = 0x50;
    build_text(c, kContinueAnsweredCaption, x, pos, scale, 0x10, 0x0B);
    stop = 0xBC;
  } else {
    x = 0x68;
    build_text(c, kTallyCaption, x, pos, scale, 0x10, 0x0B);
    stop = 0xB2;
  }
  if (timer < stop) {
    c->mem_w32(kCursorAddress,
               c->mem_r32(kCursorAddress) +
                   (uint32_t)(((stop - timer) >> 1) * (int32_t)kRecordSize));
  }
  uint32_t actor = originalPool - kRecordSize;
  for (int32_t i = 0; (int32_t)actor >= (int32_t)c->mem_r32(kCursorAddress);
       ++i, actor -= kRecordSize) {
    const int32_t phase = timer - (i + 0x8C);
    uint8_t value;
    if (phase < 0x38) {
      value = (uint8_t)(phase * 8 + 0x40);
    } else {
      const uint32_t angle = (uint32_t)(timer * 4 + i * 12) & 0xFFu;
      value = (uint8_t)(c->mem_r16(spyro::stage13_text_run::kShadeTableAddress + angle * 2u) >> 7);
    }
    c->mem_w8(actor + 0x46u, value);
  }
  return true;
}
