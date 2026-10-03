#include "product_host.h"

#include "cfg.h"
#include "core.h"
#include "dbg_server.h"
#include "game.h"
#include "picker_content.h"
#include "picker_runtime.h"
#include "picker_session.h"
#include "title_availability.h"
#include "title_runtime_registry.h"
#include "title_selection.h"
#include "title_session.h"

#include <lucent/log.h>

namespace spyro {
namespace {
// "a, b ,c" -> {"a","b","c"}; an empty string is no request at all, which means "every title".
std::vector<std::string> splitSlugs(const char *text) {
  if (text == nullptr) {
    return {};
  }
  std::vector<std::string> slugs;
  std::string current;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p == ',') {
      if (!current.empty()) {
        slugs.push_back(current);
      }
      current.clear();
      continue;
    }
    if (*p == ' ' || *p == '\t') {
      continue;
    }
    current.push_back(*p);
  }
  if (!current.empty()) {
    slugs.push_back(current);
  }
  return slugs;
}
} // namespace

ProductHost::ProductHost(std::filesystem::path provisioningRoot)
    : root_(std::move(provisioningRoot)) {
  // The window belongs to the product, so the product names it. Left unset, it would take the name
  // of whichever Game brought the device up first — the selector's — for the rest of the run.
  presentation_.setWindowTitle("Spyro");
}

int ProductHost::runSelector() {
  PickerRuntime pickerRuntime;
  const TitleAvailabilityProbe probe(root_, executableCatalog());
  for (;;) {
    // Probed on every return to the selector: a title provisioned while another ran becomes
    // available.
    const std::vector<TitleAvailability> titles = probe.probe();
    // WHICH TITLES GET A PANEL. Empty (the default) is every provisioned title, which is what a
    // player sees. PSXPORT_PICKER_TITLES=spyro1,spyro3 puts a named subset on screen: the one- and
    // two-panel selectors are real shapes, and this is how they are driven without moving anyone's
    // game files.
    const std::vector<std::string> panelSlugs = splitSlugs(cfg_str("PSXPORT_PICKER_TITLES"));
    const PickerContent content(titles, panelSlugs);
    PickerSession picker(pickerRuntime,
                         titles,
                         content,
                         cfg_int("PSXPORT_PICKER_FRAMES", 0),
                         presentation_.device());
    const PickerSession::Result choice = picker.run();
    if (choice.outcome != PickerSession::Outcome::Chosen || choice.session == nullptr) {
      return 0;
    }
    // The session that was playing its demo in the panel IS the session that runs now: nothing is
    // rebooted, and every other title's session died with the selector that owned them.
    lucent::info("host",
                 "starting {} ({})",
                 choice.session->title().identity->displayName,
                 choice.session->title().identity->serial);
    const int status = runToEnd(*choice.session);
    if (status != 0) {
      return status; // a boot refusal is not something the selector can retry
    }
    // Back to the top: the title finished or asked for the selector, and the selector is rebuilt —
    // with a fresh probe, so a title provisioned in the meantime gets a panel this time.
  }
}

int ProductHost::runToEnd(TitleSession &session) {
  if (!session.boot()) {
    lucent::error("boot", "{} could not be started", session.title().identity->displayName);
    return 2;
  }
  // The selector owned the process debug endpoint and is gone by now (its Game died with the
  // PickerSession above). This session is the product, so it takes the endpoint — otherwise the
  // channel answers nobody for the rest of the run.
  session.claimDebugEndpoint();
  while (session.end() == TitleSession::End::Running) {
    session.step();
  }
  // Destruction is the whole teardown: the machine, the pad, the CD stream, the memory card and the
  // debug endpoint all die with this object, and the window and its device — the product's —
  // survive into the next session. Whether the run finished or returned to the selector is the
  // CALLER's question, not this loop's.
  return 0;
}

int ProductHost::runExecutable(const std::filesystem::path &executable) {
  const SelectionResult selection = selectExecutableFile(executable, executableCatalog());
  if (!selection) {
    lucent::error("boot", "{}", selection.detail);
    return 2;
  }
  TitleAvailability title;
  title.identity = selection.identity;
  title.executable = executable;
  title.status = AvailabilityStatus::Available;
  TitleSession session(title, false, presentation_.device());
  runToEnd(session);
  return 0;
}

} // namespace spyro
