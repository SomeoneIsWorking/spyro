#pragma once
#include <cstdint>

enum class BootSkipAction : uint8_t { None, Baseline, AdvancePresentation };

struct BootSkipState {
  bool active = false;
  bool sampled = false;
  bool previousStartDown = false;
  uint32_t fields = 0;
  uint32_t edges = 0;
  uint32_t advances = 0;
};

inline void boot_skip_begin(BootSkipState& s) { s = {}; s.active = true; }

inline BootSkipAction boot_skip_sample(BootSkipState& s, bool startDown) {
  if (!s.active) return BootSkipAction::None;
  ++s.fields;
  if (!s.sampled) {
    s.sampled = true;
    s.previousStartDown = startDown;
    return BootSkipAction::Baseline;
  }
  const bool edge = startDown && !s.previousStartDown;
  s.previousStartDown = startDown;
  if (!edge) return BootSkipAction::None;
  ++s.edges;
  ++s.advances;
  return BootSkipAction::AdvancePresentation;
}

int spyro_boot_skip_selftest();
