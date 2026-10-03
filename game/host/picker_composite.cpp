#include "picker_composite.h"

#include "game.h"
#include "gpu_vk_device.h"

#include <lucent/log.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace spyro {
namespace {

// The unselected panel is GREY, not tinted: R == G == B, with no colour left to read as "this one
// is the game". It was 0.85, which left an eighth of the hue behind — a teal sea still measurably
// teal, and Insomniac's maroon still maroon, which is exactly the failure a desaturation of "most
// of the way" produces. FULL is the value that means grey.
constexpr float kUnselectedDesaturation = 1.0f;
constexpr float kUnselectedBrightness = 0.78f;

// THE PANELS TILE THE WINDOW, SO THERE IS NO BACKDROP. A panel is the surface's own height and a
// quarter or half of its width, and its picture fills it; anything the picture cannot cover is
// cropped away rather than letterboxed, because a black band above and below each panel is what
// stopped three games reading as three games filling the window. The slanted divider is the one
// piece of host furniture left, and it lies ON a seam between two panels.

// WHERE THE LOGO SITS is the layout's: centred on its panel, a fifth of the way down it. This only
// sizes the logo to its panel.

// The dividers: a thin authored line, half a percent of the surface height (at least one
// pixel), in the same gold the picker's screen uses for the selection so the line and the name
// agree about which panel is the chosen one.
constexpr float kDividerPerHeight = 0.005f;
constexpr int kDividerMinimumPixels = 1;
constexpr float kDividerRed = 0.76f;
constexpr float kDividerGreen = 0.64f;
constexpr float kDividerBlue = 0.18f;

// A logo drawn over a running demo is furniture ON the picture, so it is fully opaque: it is the
// title's own wordmark, and a wordmark half faded into a demo is not a wordmark.
constexpr float kLogoAlpha = 1.0f;

// THE SCALE THE PANEL APPLIES TO ITS PICTURE, AND SO TO ITS LOGO. A panel's picture is a 240-line
// guest frame cover-cropped into a panel that is as tall as the window, so the panel magnifies it
// by cover.h / crop.h — 3 in a 960x720 window. A wordmark drawn at its own texel size beside that
// picture is the size the game NEVER shows it at: on the console, and in the panel's own picture,
// that wordmark is drawn by the same GPU at the same scale as everything else in the frame. So the
// logo is magnified by the same factor, and by whole texels when the factor is whole, so the logo
// keeps the game's pixel grid instead of being interpolated by the pane's linear sampler.
int pictureMagnification(const PanelLayout &geometry,
                         const spyro::SourceCrop &crop,
                         int pictureHeight) {
  // `crop` is in FRACTIONS of the picture, so the rows a panel keeps is crop.h * pictureHeight —
  // dividing by crop.h alone is dividing by a number below 1, which asks for a magnification of a
  // hundred thousand and allocates a hundred-thousand-times-too-large image.
  const double rows = static_cast<double>(crop.h) * static_cast<double>(pictureHeight);
  if (rows <= 0.0 || geometry.cover.h <= 0) {
    return 1;
  }
  const double scale = static_cast<double>(geometry.cover.h) / rows;
  const int whole = static_cast<int>(std::lround(scale));
  // A panel is never a hundred times its picture; anything past that is a number that went wrong,
  // and magnifying by it would allocate the wrong image rather than draw one.
  if (whole > 16) {
    return 1;
  }
  // Only a scale that IS whole is applied texel by texel; anything else is left to the draw, which
  // is what the picture itself does, so the logo and the picture never disagree about
  // magnification.
  return whole >= 1 && std::abs(scale - whole) < 0.02 ? whole : 1;
}

// How wide a panel's logo is drawn: its own aspect, scaled to the panel's own width, so the
// wordmark is as wide as the game it names and shrinks with the panel as the widths animate. A logo
// wider than the panel would be cut by the seams, so it is fitted to the panel's usable width.
int logoWidthAt(const PanelLogo &logo, int panelWidth) {
  if (logo.empty() || logo.height() <= 0) {
    return 0;
  }
  if (panelWidth <= 0) {
    return logo.width();
  }
  return std::max(1, std::min(panelWidth, logo.width()));
}

} // namespace

PickerComposite::PickerComposite(GpuDevice &device, Core *hostCore)
    : m_compositor(device, hostCore), m_device(device) {}

PickerComposite::~PickerComposite() = default;

void PickerComposite::setSurface(const PickerLayout &layout, int width, int height) {
  m_surfaceW = std::max(width, 0);
  m_surfaceH = std::max(height, 0);
  m_paneImageW = layout.maxPanelWidth(m_surfaceW);
  m_paneImageH = m_surfaceH;
}

void PickerComposite::present(const PickerLayout &layout,
                              std::span<const PanelSource> panels,
                              int selected) {
  m_panes.clear();
  m_dividers.clear();
  // One entry per panel, then one divider per BOUNDARY: a boundary belongs to the panel on its
  // left, so two neighbours can never draw two different lines in the same place.
  m_panes.reserve(panels.size() * 2);
  m_dividers.reserve(panels.size());
  const int dividerPixels =
      std::max(kDividerMinimumPixels, static_cast<int>(m_surfaceH * kDividerPerHeight));
  for (std::size_t index = 0; index < panels.size(); ++index) {
    const PanelSource &source = panels[index];
    const bool isSelected = static_cast<int>(index) == selected;
    const PanelLayout geometry =
        layout.panel(static_cast<int>(index), source.pictureWidth, source.pictureHeight);

    // THE PANEL'S OWN SEAMS. Every pane in a panel — picture and logo — is cut by the same
    // two lines, so the panel is one column with one outline and nothing inside it can leak over a
    // neighbour. The seams are the layout's numbers in surface pixels, handed to the compositor
    // unchanged: the layout owns where the boundary is, and the compositor only decides which side
    // of it a fragment is on.
    const auto seamsOf = [&geometry](psxport::Pane &pane) {
      pane.seamLeftTop = static_cast<float>(geometry.seams.leftTopX);
      pane.seamLeftBottom = static_cast<float>(geometry.seams.leftBottomX);
      pane.seamRightTop = static_cast<float>(geometry.seams.rightTopX);
      pane.seamRightBottom = static_cast<float>(geometry.seams.rightBottomX);
    };

    // THE PICTURE FILLS THE PANEL. The pane IS the panel's own rectangle, from the surface's top
    // edge to its bottom edge, with no backdrop behind it and no letterbox: the panels tile the
    // window edge to edge, and what the picture cannot cover is cropped away rather than left as a
    // hole. How much is cropped is the LAYOUT's number, not this one's, and it is what keeps the
    // aspect while filling the shape.
    psxport::Pane pane{};
    pane.core = source.session;
    pane.originX = static_cast<float>(geometry.cover.x);
    pane.originY = static_cast<float>(geometry.cover.y);
    pane.axisUX = static_cast<float>(geometry.cover.w);
    pane.axisUY = 0.0f;
    pane.axisVX = 0.0f;
    pane.axisVY = static_cast<float>(geometry.cover.h);
    const spyro::SourceCrop crop = geometry.sourceCrop(source.pictureWidth, source.pictureHeight);
    pane.sourceU = crop.u;
    pane.sourceV = crop.v;
    pane.sourceW = crop.w;
    pane.sourceH = crop.h;
    seamsOf(pane);
    // GREY, not tinted. An unselected panel is the same picture with its colour taken out, which is
    // what "not this one" reads as from across a room; the slight drop in brightness below only
    // finishes the job, it does not do it. This is a per-pane uniform sampled in the pane shader,
    // so it costs nothing on the host and no readback of the picture.
    pane.desaturation = isSelected ? 0.0f : kUnselectedDesaturation;
    const float brightness = isSelected ? 1.0f : kUnselectedBrightness;
    pane.tintR = brightness;
    pane.tintG = brightness;
    pane.tintB = brightness;
    m_panes.push_back(pane);

    // THE TITLE'S OWN LOGO, taken from that title's own disc at runtime and drawn over its panel:
    // horizontally centred on the panel, with its own centre a fifth of the way
    // down it. It is drawn OVER the picture, so it blends with it rather than replacing it,
    // and it takes the panel's own grey when the panel is not the selected one — a colour logo over
    // a grey panel would name the chosen title on the wrong column.
    if (source.logo != nullptr && !source.logo->empty()) {
      const int magnification = pictureMagnification(geometry, crop, source.pictureHeight);
      // The magnified copy is the logo's own, cached inside it: the panel asks for it every frame
      // and the logo it is asked about never changes, so this costs one magnification, not one a
      // frame.
      const PanelLogo *const drawn = &source.logo->magnifiedNearest(magnification);
      const int logoWidth = logoWidthAt(*drawn, geometry.averageWidth());
      const int logoHeight =
          std::max(1,
                   static_cast<int>(std::lround(static_cast<double>(logoWidth) *
                                                static_cast<double>(drawn->height()) /
                                                static_cast<double>(drawn->width()))));
      const spyro::PanelRect logoBox = geometry.logoRect(logoWidth, logoHeight);
      psxport::Pane logo{};
      logo.texture = drawn->texture(m_device);
      logo.originX = static_cast<float>(logoBox.x);
      logo.originY = static_cast<float>(logoBox.y);
      logo.axisUX = static_cast<float>(logoBox.w);
      logo.axisUY = 0.0f;
      logo.axisVX = 0.0f;
      logo.axisVY = static_cast<float>(logoBox.h);
      // The logo is the panel's own image scaled to the panel's own width, so it is drawn from the
      // whole logo and never from a crop of it.
      logo.desaturation = isSelected ? 0.0f : kUnselectedDesaturation;
      logo.tintR = brightness;
      logo.tintG = brightness;
      logo.tintB = brightness;
      logo.alpha = kLogoAlpha;
      seamsOf(logo);
      lucent::debug(
          "picker",
          "logo panel {} image {}x{} magnified {}x drawn {}x{} at {},{} cover {}x{} at {},{}",
          index,
          source.logo->width(),
          source.logo->height(),
          magnification,
          logoBox.w,
          logoBox.h,
          logoBox.x,
          logoBox.y,
          geometry.cover.w,
          geometry.cover.h,
          geometry.cover.x,
          geometry.cover.y);
      m_panes.push_back(logo);
    }

    if (index + 1 >= panels.size()) {
      continue;
    }
    // THE DIVIDER is recorded and NOT YET APPENDED: see the pass order below. A divider lies on a
    // seam the NEXT panel's backdrop also covers, so drawing it inline means the panel it separates
    // paints over it.
    const float seamTop = static_cast<float>(geometry.seams.rightTopX);
    const float seamBottom = static_cast<float>(geometry.seams.rightBottomX);
    const float half = static_cast<float>(dividerPixels) * 0.5f;
    psxport::Pane divider{};
    divider.solid = true;
    // THE RECTANGLE MUST ENCLOSE THE MASK, or the mask erases the pane. This line leans, so the
    // strip it is drawn in spans from the top of the seam to the bottom of it: a rect as narrow as
    // the line itself sits at the TOP of the seam and is cut away by every row below, which is how
    // the divider came to be a gold mark a few pixels tall at the top of the window and nothing
    // below.
    const float bandLeft = std::min(seamTop, seamBottom) - half;
    const float bandRight = std::max(seamTop, seamBottom) + half;
    divider.originX = bandLeft;
    divider.originY = 0.0f;
    divider.axisUX = bandRight - bandLeft;
    divider.axisUY = 0.0f;
    divider.axisVX = 0.0f;
    divider.axisVY = static_cast<float>(m_surfaceH);
    // The line is the shared seam, half a divider's width either side of it: two neighbours cut by
    // the same numbers, so it is drawn once and both agree where it is.
    divider.seamLeftTop = seamTop - half;
    divider.seamLeftBottom = seamBottom - half;
    divider.seamRightTop = seamTop + half;
    divider.seamRightBottom = seamBottom + half;
    divider.tintR = kDividerRed;
    divider.tintG = kDividerGreen;
    divider.tintB = kDividerBlue;
    m_dividers.push_back(divider);
  }

  // THE PASS ORDER IS THE WHOLE TRICK, and getting it wrong is invisible in the code and obvious on
  // the screen. Every panel's backdrop, picture and logo first, and only then every divider: a
  // divider sits ON a seam that the next panel's backdrop also covers, so a divider drawn inline is
  // painted over by the panel it separates — which is what it looked like, a gold line that stopped
  // a few pixels below the top of the window.
  for (const psxport::Pane &divider : m_dividers) {
    m_panes.push_back(divider);
  }
  m_dividers.clear();

  // The picture geometry the compositor is actually drawing from, once per present when the
  // channel is on: the presented image, its viewport (where the picture sits inside it) and the
  // rectangle it is fitted into. Black bands at a panel's top and bottom are either the guest's own
  // rows or this rectangle being wrong, and this line is which.
  if (lucent::detail::channel_enabled("picker")) {
    for (std::size_t index = 0; index < panels.size(); ++index) {
      const PanelSource &source = panels[index];
      const GpuVkState::PresentedImage image =
          source.session != nullptr ? source.session->game->gpu_vk.lastFilledPresented()
                                    : GpuVkState::PresentedImage{};
      const PanelLayout geometry =
          layout.panel(static_cast<int>(index), source.pictureWidth, source.pictureHeight);
      lucent::debug("picker",
                    "panel {} image {}x{} viewport {}x{} at +{}+{} into {}x{} at +{}+{} held={}",
                    index,
                    image.width,
                    image.height,
                    image.viewport.w,
                    image.viewport.h,
                    image.viewport.x,
                    image.viewport.y,
                    geometry.cover.w,
                    geometry.cover.h,
                    geometry.cover.x,
                    geometry.cover.y,
                    image.valid() ? "yes" : "no");
    }
  }

  m_compositor.composite(m_panes);
}

void PickerComposite::captureShot(const char *path) {
  m_compositor.presentShot(path);
}

} // namespace spyro