#include "product_host.h"

#include "cfg.h"
#include "picker_content.h"
#include "picker_runtime.h"
#include "picker_session.h"
#include "title_availability.h"
#include "title_runtime_registry.h"
#include "title_selection.h"
#include "title_session.h"

#include <lucent/log.h>

namespace spyro {

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
    const PickerContent content(titles);
    PickerSession picker(
        pickerRuntime, content, cfg_int("PSXPORT_PICKER_FRAMES", 0), presentation_.device());
    const PickerSession::Result choice = picker.run();
    if (choice.outcome != PickerSession::Outcome::Chosen) {
      return 0;
    }
    lucent::info("host",
                 "starting {} ({})",
                 choice.title->identity->displayName,
                 choice.title->identity->serial);
    TitleSession session(*choice.title, true, presentation_.device());
    if (session.run() == TitleSession::End::Finished) {
      return 0;
    }
  }
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
  session.run();
  return 0;
}

} // namespace spyro
