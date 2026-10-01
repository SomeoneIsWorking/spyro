#pragma once

#include "title_availability.h"

#include <choice_view.h>
#include <optional>
#include <span>
#include <string_view>

namespace spyro {

// The selector's screen for a probed catalog, and the inverse map from a chosen entry back to a
// title. Entry i of the content is catalog entry i of `titles`; nothing else about a label is an
// identity.
class PickerContent {
public:
  explicit PickerContent(std::span<const TitleAvailability> titles);

  const psx::ui::ChoiceContent &content() const {
    return content_;
  }
  const TitleAvailability &title(int index) const;
  // The AVAILABLE title with this slug, for the control channel's `pick <slug>`. Names the refusal
  // when the slug is unknown or the title cannot be started.
  const TitleAvailability *findAvailable(std::string_view slug, std::string &refusal) const;
  std::string listing() const;

private:
  std::span<const TitleAvailability> titles_;
  psx::ui::ChoiceContent content_;
};

} // namespace spyro
