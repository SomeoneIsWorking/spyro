#include "picker_content.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace spyro {
namespace {

// One screen entry per title. `enabled`/`reason` are the CATALOG's answers (a title that cannot be
// started stays visible carrying its reason); a PANEL entry is always enabled, because a title that
// cannot be started has no panel to be disabled in the first place.
psx::ui::ChoiceEntry entryFor(const TitleAvailability &title, bool asPanel) {
  psx::ui::ChoiceEntry entry;
  entry.label = std::string(title.identity->displayName);
  entry.detail = std::string(title.identity->serial);
  entry.enabled = asPanel ? true : title.available();
  entry.reason = title.reason;
  return entry;
}

} // namespace

namespace {
// Whether `slug` is one of the requested panel titles. An empty request means "all of them".
bool wanted(std::span<const std::string> slugs, std::string_view slug) {
  if (slugs.empty()) {
    return true;
  }
  return std::find(slugs.begin(), slugs.end(), slug) != slugs.end();
}
} // namespace

PickerContent::PickerContent(std::span<const TitleAvailability> titles,
                             std::span<const std::string> panelSlugs)
    : titles_(titles) {
  content_.heading = "Spyro";
  content_.hint = "Left / Right: choose   -   Cross / Start: play";
  panelContent_ = content_;
  for (int index = 0; index < static_cast<int>(titles_.size()); ++index) {
    const TitleAvailability &title = titles_[static_cast<std::size_t>(index)];
    content_.entries.push_back(entryFor(title, false));
    if (!title.available() || !wanted(panelSlugs, title.identity->slug)) {
      continue;
    }
    panels_.push_back(index);
    panelContent_.entries.push_back(entryFor(title, true));
  }
}

const TitleAvailability &PickerContent::title(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= titles_.size()) {
    throw std::out_of_range("picker entry index outside the probed catalog");
  }
  return titles_[static_cast<std::size_t>(index)];
}

const TitleAvailability &PickerContent::panelTitle(int panel) const {
  if (panel < 0 || static_cast<std::size_t>(panel) >= panels_.size()) {
    throw std::out_of_range("picker panel index outside the panels that exist");
  }
  return title(panels_[static_cast<std::size_t>(panel)]);
}

int PickerContent::panelOf(std::string_view slug) const {
  for (int panel = 0; panel < static_cast<int>(panels_.size()); ++panel) {
    if (panelTitle(panel).identity->slug == slug) {
      return panel;
    }
  }
  return -1;
}

const TitleAvailability *PickerContent::findAvailable(std::string_view slug,
                                                      std::string &refusal) const {
  for (const TitleAvailability &title : titles_) {
    if (title.identity->slug != slug) {
      continue;
    }
    if (!title.available()) {
      refusal = std::format("{} cannot be started: {}", slug, title.reason);
      return nullptr;
    }
    return &title;
  }
  refusal = std::format("unknown title '{}'", slug);
  return nullptr;
}

std::string PickerContent::listing() const {
  std::string out;
  for (const TitleAvailability &title : titles_) {
    out += std::format("{} {} {}{}\n",
                       title.identity->slug,
                       title.identity->serial,
                       title.available() ? "available" : "disabled",
                       title.available() ? "" : " (" + title.reason + ")");
  }
  return out;
}

} // namespace spyro
