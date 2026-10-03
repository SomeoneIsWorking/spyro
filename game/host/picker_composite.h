// picker_composite.h — the picker's COMPOSITING: one window frame with a panel per title in it.
//
// WHAT THIS OWNS. PanelSessions decides which guest runs; PickerLayout decides where each panel is.
// Neither of them can draw. This is the seam between them and the framework's pane compositor
// (psxport::PaneCompositor), and it owns three things they do not: the BACKDROP each panel's fitted
// picture sits on, the tint that makes an unselected panel grey, and the SLANTED DIVIDERS between
// panels.
//
// EVERYTHING IN A PANEL IS AN UPRIGHT RECTANGLE CUT BY THE PANEL'S OWN SEAMS. The layout gives two
// boundary lines per panel and the compositor passes them straight into the pane pass, where the
// fragment stage drops whatever falls outside them. Nothing inside a panel is sheared: the game's
// picture is the game's own geometry at its own aspect, and the seam is decoration drawn over it.
//
// NO TEXT. The panel is named by the title's OWN logo, taken from that title's own disc at runtime
// (panel_logo.h) and drawn over its panel — not by a caption, a heading or a control hint, all of
// which were words on top of three running demos.
//
// The dividers are host furniture, not a title's picture: they are drawn through the same pane pass
// as a solid colour from the compositor's own white source, which is what keeps one contract for "a
// shape in this colour" instead of a second pipeline with its own. Each divider is drawn ONCE per
// boundary, on the panel to its left, so the two neighbours can never disagree about where the line
// between them is.
#pragma once

#include "panel_logo.h"
#include "picker_layout.h"

#include "pane_composite.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class Core;
class GpuDevice;

namespace spyro {

// One panel's contribution to a frame.
struct PanelSource {
  Core *session = nullptr; // null while the panel has no picture of its own
  int pictureWidth = 1;    // the picture's own aspect, in pixels
  int pictureHeight = 1;
  // The title's own logo, decoded once from its own disc. Null until the panel's session has been
  // far enough into its boot for the guest to have drawn it; the panel then shows no name at all,
  // which is the truth, rather than a placeholder wordmark.
  PanelLogo *logo = nullptr;
};

class PickerComposite {
public:
  PickerComposite(GpuDevice &device, Core *hostCore);
  ~PickerComposite();
  PickerComposite(const PickerComposite &) = delete;
  PickerComposite &operator=(const PickerComposite &) = delete;

  // The surface the panes are placed in, in sink pixels. Every panel's present image is built at
  // the SELECTED panel's widest size (a panel is never wider than that), which is why this takes
  // the layout: the sessions are told the image size here, before any of them steps, so the first
  // frame a panel shows is already the right size rather than a full-window image resized a moment
  // later.
  void setSurface(const PickerLayout &layout, int width, int height);
  int surfaceWidth() const {
    return m_surfaceW;
  }
  int surfaceHeight() const {
    return m_surfaceH;
  }
  // The present-image size a panel session should build its picture at.
  int paneImageWidth() const {
    return m_paneImageW;
  }
  int paneImageHeight() const {
    return m_paneImageH;
  }

  // Build the frame: every panel's backdrop, its picture, its title's own logo, and the dividers.
  void present(const PickerLayout &layout, std::span<const PanelSource> panels, int selected);

  // Write the composited frame to a file: WHAT THE PLAYER SEES of the selector, in either leg.
  void captureShot(const char *path);

private:
  psxport::PaneCompositor m_compositor;
  GpuDevice &m_device;
  std::vector<psxport::Pane> m_panes;
  // The dividers, held back until every panel has been drawn: see the pass order in the .cpp.
  std::vector<psxport::Pane> m_dividers;
  int m_surfaceW = 0, m_surfaceH = 0;
  int m_paneImageW = 0, m_paneImageH = 0;
};

} // namespace spyro
