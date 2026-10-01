#include "picker_content.h"

#include <format>
#include <stdexcept>
#include <string>

namespace spyro {

PickerContent::PickerContent(std::span<const TitleAvailability> titles) : titles_(titles) {
  content_.heading = "Spyro";
  content_.hint = "Up / Down: choose   -   Cross / Start / Enter: play";
  for (const TitleAvailability &title : titles_) {
    psx::ui::ChoiceEntry entry;
    entry.label = std::string(title.identity->displayName);
    entry.detail = std::string(title.identity->serial);
    entry.enabled = title.available();
    entry.reason = title.reason;
    content_.entries.push_back(std::move(entry));
  }
}

const TitleAvailability &PickerContent::title(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= titles_.size()) {
    throw std::out_of_range("picker entry index outside the probed catalog");
  }
  return titles_[static_cast<std::size_t>(index)];
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
