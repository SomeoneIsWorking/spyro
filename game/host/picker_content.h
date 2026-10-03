#pragma once

#include "title_availability.h"

#include <choice_view.h>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace spyro {

// The selector's content for a probed catalog, and the inverse map from a chosen entry back to a
// title. Entry i of `content()` is catalog entry i of `titles`; nothing else about a label is an
// identity.
//
// TWO LISTS, ON PURPOSE. `content()` is the CATALOG: every title, enabled or not, each carrying the
// reason it cannot be started — that is what the control channel lists and what answers "why is
// Spyro 3 greyed out?". `panelContent()` is the PICKER's own: one entry per available title, in
// catalog order, which is the same order the panels are laid out in, so panel N is entry N of that
// list. A panel that cannot be started does not get one.
class PickerContent {
public:
  // `panelSlugs` restricts which of the available titles get a PANEL; empty means every available
  // one. It changes what the selector shows, never what can be started: `pick <slug>` still
  // resolves any provisioned title, so this is a way to put fewer titles on screen (a one- and
  // two-panel selector are real shapes) without touching the provisioned files themselves.
  PickerContent(std::span<const TitleAvailability> titles,
                std::span<const std::string> panelSlugs = {});

  const psx::ui::ChoiceContent &content() const {
    return content_;
  }
  // One entry per PANEL: the available titles, in catalog order.
  const psx::ui::ChoiceContent &panelContent() const {
    return panelContent_;
  }
  const TitleAvailability &title(int index) const;
  // The title behind panel `panel`, or the same out_of_range refusal `title` gives.
  const TitleAvailability &panelTitle(int panel) const;
  // The panel showing this slug, or -1 when the slug names no panel (unknown, or not startable).
  int panelOf(std::string_view slug) const;
  int panelCount() const {
    return static_cast<int>(panels_.size());
  }
  // The AVAILABLE title with this slug, for the control channel's `pick <slug>`. Names the refusal
  // when the slug is unknown or the title cannot be started.
  const TitleAvailability *findAvailable(std::string_view slug, std::string &refusal) const;
  std::string listing() const;

private:
  std::span<const TitleAvailability> titles_;
  std::vector<int> panels_; // catalog index of each panel, in panel order
  psx::ui::ChoiceContent content_;
  psx::ui::ChoiceContent panelContent_;
};

} // namespace spyro
