// SPDX-License-Identifier: GPL-3.0-or-later
// panel_logo.h — ONE title's OWN LOGO, snapshotted out of that title's own live VRAM while the
// game's own title screen is on it.
//
// WHAT THIS IS. A title's logo is not a picture this project ships: it is artwork inside the
// player's own disc, which the title itself decodes during its boot, uploads to VRAM, and draws on
// its title screen. The picker shows that artwork as the name of the panel it belongs to — so the
// words on a panel are the game's own, in the game's own art, and nothing about them is committed
// to this repository, fetched from a network, or copied out of a disc image.
//
// HOW IT IS OBTAINED. The facts name the title's own GATE — the guest word whose tick is what
// retail itself tests to decide its logo is up — and the sprite records the game's emitter reads
// for its wordmark. While the gate is open, `extractPanelLogo` reads those records out of the live
// session's RAM and snapshots exactly their texture rectangles and their CLUTs out of that
// session's VRAM: the texture the game uploaded, resolved through the palette the game uploaded,
// with index 0 as the PSX's transparency rule says. That is the game's own uploaded texture, not a
// picture inferred from the composited framebuffer.
//
// WHEN IT IS TAKEN. Exactly once, on the first panel step whose gate is open. A gate that never
// opens yields no logo and says so in the log; it is never faked, and no other frame is ever looked
// at.
#pragma once

#include "title_logo_facts.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <memory>
#include <vector>

class Core;
class GpuDevice;

namespace spyro {

// The snapshot of one title's OWN LOGO: an RGBA8 image in memory, and the GPU texture the picker
// draws it from. Owned by the panel that shows it, so a logo dies with the screen it is on.
class PanelLogo {
public:
  // `rgba` is `width * height * 4` bytes, premultiplied or not — the compositor draws it with its
  // own blend, so this only has to be straight alpha in the host's usual order.
  PanelLogo(int width, int height, std::vector<std::uint8_t> rgba);
  ~PanelLogo();
  PanelLogo(const PanelLogo &) = delete;
  PanelLogo &operator=(const PanelLogo &) = delete;

  bool empty() const {
    return m_width <= 0 || m_height <= 0;
  }
  int width() const {
    return m_width;
  }
  int height() const {
    return m_height;
  }
  // THE SAME ART AT THE SIZE THE GAME SHOWS IT. `factor` is the integer the panel's picture is
  // scaled by (a 240-line frame into a 720-line panel is 3), and the pixels are replicated by it —
  // nearest neighbour, no blending — so the wordmark on a panel is the game's own texels at the
  // game's own scale rather than a smooth interpolation of them. A panel asks for this EVERY frame
  // and the logo it asks about never changes, so the magnified copy is built the first time it is
  // asked for and kept until the logo dies: one logo, one magnification, not one per frame.
  // Returns `*this` when there is nothing to magnify.
  const PanelLogo &magnifiedNearest(int factor) const;
  // The GPU texture, created on first use against `device` and destroyed with this object. Const
  // because the PIXELS never change: creating the texture is a cache being filled, not the image
  // being altered, so a const logo can hand out its texture and a const panel can draw one.
  SDL_GPUTexture *texture(GpuDevice &device) const;

private:
  int m_width;
  int m_height;
  std::vector<std::uint8_t> m_rgba;
  // The device these pixels were uploaded to. A texture belongs to the device that made it, so it
  // is kept beside the texture and not re-derived: releasing through another device's context is
  // exactly the sort of thing that works until the window is recreated.
  mutable SDL_GPUDevice *m_textureDevice = nullptr;
  mutable SDL_GPUTexture *m_texture = nullptr;
  // The magnified copy, and the factor it was built for. Both are lazy and both belong to THIS
  // logo: a logo is immutable once it exists, so there is exactly one correct answer per factor
  // and it never has to be recomputed. A factor of 0 means "not built yet".
  mutable std::unique_ptr<PanelLogo> m_magnified;
  mutable int m_magnifiedFactor = 0;
};

// THE SNAPSHOT: one title's own logo, taken from that title's own live session.
//
// `core` is the panel's LIVE session. `facts` is the title's own (see TitleLogoFacts): the gate to
// test and the sprite records to read.
//
// Returns null — never an empty image dressed as a logo — when the facts name nothing, when the
// title's gate is not open yet (that is the normal answer during the rest of a panel's life, and it
// is not logged as a failure: the caller asks again on the next step), or when the snapshot comes
// back with no artwork in it.
std::unique_ptr<PanelLogo> extractPanelLogo(Core &core, const TitleLogoFacts &facts);

// Whether the title's own logo gate is open in this session right now. Split out because the panel
// asks it every step and must not pay for a snapshot it cannot take.
bool titleLogoGateOpen(Core &core, const TitleLogoFacts &facts);

} // namespace spyro