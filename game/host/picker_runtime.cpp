#include "picker_runtime.h"

#include <cctype>
#include <cstring>

namespace spyro {

bool PickerRuntime::controlCommand(Core &, const char *cmd, const char *line, FILE *out) {
  return handle(cmd, line, out);
}

bool PickerRuntime::handle(const char *cmd, const char *line, FILE *out) {
  if (!content_) {
    return false;
  }
  if (std::strcmp(cmd, "picker") == 0) {
    const std::string listing = content_->listing();
    std::fputs(listing.c_str(), out);
    return true;
  }
  if (std::strcmp(cmd, "pick") == 0) {
    const char *args = line + std::strlen(cmd);
    while (*args == ' ' || *args == '\t') {
      ++args;
    }
    std::string slug(args);
    while (!slug.empty() && std::isspace(static_cast<unsigned char>(slug.back()))) {
      slug.pop_back();
    }
    std::string refusal;
    const TitleAvailability *title = content_->findAvailable(slug, refusal);
    if (!title) {
      std::fprintf(out, "refused: %s\n", refusal.c_str());
    } else {
      pendingSlug_ = std::string(title->identity->slug);
      std::fprintf(out, "ok: starting %s\n", pendingSlug_->c_str());
    }
    return true;
  }
  return false;
}

} // namespace spyro
