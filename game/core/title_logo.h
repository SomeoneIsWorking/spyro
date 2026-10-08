// SPDX-License-Identifier: GPL-3.0-or-later
// A title's own logo, snapshotted from its live VRAM while its title screen is on it.
#pragma once

#include "logo_image.h"
#include "title_logo_facts.h"

#include <optional>

class Core;

namespace spyro {

// Snapshots the sprite rectangles and CLUTs the named gate's emitter records point at, reading
// the texture the game uploaded to VRAM rather than inferring art from the framebuffer.
// Returns nullopt — never an empty image dressed as a logo — when there is nothing to read yet.
std::optional<psx::host::LogoImage> extractPanelLogo(Core &core, const TitleLogoFacts &facts);

// Whether the logo gate is open right now; split out so a step can test it without a snapshot.
bool titleLogoGateOpen(Core &core, const TitleLogoFacts &facts);

} // namespace spyro
