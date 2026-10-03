// The title selector's decisions, with no window: which catalog entries are startable and why not,
// how an entry maps back to a title, and what the control-channel `pick` accepts and refuses.
#include "picker_content.h"
#include "picker_runtime.h"
#include "title_availability.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void writeU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8u));
  }
}

// The same authenticated fixture tests/test_title_selection.cpp uses; its SHA-256 is in the catalog
// below.
std::vector<std::uint8_t> executable() {
  std::vector<std::uint8_t> bytes(0x800u);
  constexpr std::array<std::uint8_t, 8> magic{'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
  std::copy(magic.begin(), magic.end(), bytes.begin());
  writeU32(bytes, 0x10u, 0x80010100u);
  writeU32(bytes, 0x18u, 0x80010000u);
  writeU32(bytes, 0x1Cu, 0x1000u);
  writeU32(bytes, 0x30u, 0x801FFFF0u);
  return bytes;
}

void write(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

spyro::ExecutableIdentity identity(spyro::SpyroTitle title,
                                   const char *name,
                                   const char *serial,
                                   const char *slug,
                                   const char *sha) {
  return spyro::ExecutableIdentity{.title = title,
                                   .displayName = name,
                                   .serial = serial,
                                   .slug = slug,
                                   .fileSize = 0x800u,
                                   .sha256 = sha,
                                   .entry = 0x80010100u,
                                   .globalPointer = 0u,
                                   .textAddress = 0x80010000u,
                                   .textSize = 0x1000u,
                                   .stackAddress = 0x801FFFF0u,
                                   .stackOffset = 0u};
}

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("spyro_picker_test_" + std::to_string(::getpid()));
  std::filesystem::remove_all(root);
  const std::array catalog{
      identity(spyro::SpyroTitle::Spyro1,
               "One",
               "SCUS_000.01",
               "alpha",
               "02e23f3624a575943098a80ec71a271d1d192128907fd3bd464a2f54b523239a"),
      identity(spyro::SpyroTitle::Spyro2,
               "Two",
               "SCUS_000.02",
               "beta",
               "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
      identity(spyro::SpyroTitle::Spyro3,
               "Three",
               "SCUS_000.03",
               "gamma",
               "02e23f3624a575943098a80ec71a271d1d192128907fd3bd464a2f54b523239a"),
  };
  // alpha: provisioned and authentic. beta: absent. gamma: present but the bytes are not gamma's.
  std::vector<std::uint8_t> good = executable();
  write(root / "alpha" / "SCUS_000.01", good);
  std::vector<std::uint8_t> bad = good;
  bad.back() = 1u;
  write(root / "gamma" / "SCUS_000.03", bad);

  const spyro::TitleAvailabilityProbe probe(root, catalog);
  const std::vector<spyro::TitleAvailability> titles = probe.probe();
  expect(titles.size() == 3, "one entry per catalog title");
  expect(titles[0].available() && titles[0].reason.empty(),
         "authentic provisioned title is available");
  expect(titles[1].status == spyro::AvailabilityStatus::NotProvisioned && !titles[1].reason.empty(),
         "absent executable is disabled with a reason");
  expect(titles[2].status == spyro::AvailabilityStatus::IdentityMismatch &&
             titles[2].reason.find("SCUS_000.03") != std::string::npos,
         "present-but-wrong bytes are disabled and name the expected serial");

  const spyro::PickerContent content(titles);
  const auto &entries = content.content().entries;
  expect(entries.size() == 3 && entries[0].enabled && !entries[1].enabled && !entries[2].enabled,
         "entry enablement follows availability");
  expect(entries[1].reason == titles[1].reason, "a disabled entry shows why");
  expect(content.title(0).identity->slug == "alpha", "entry index maps back to its catalog title");
  bool outOfRange = false;
  try {
    (void)content.title(3);
  } catch (const std::out_of_range &) {
    outOfRange = true;
  }
  expect(outOfRange, "an index outside the catalog is refused, not clamped");

  std::string refusal;
  expect(content.findAvailable("alpha", refusal) == &titles[0],
         "pick of an available slug resolves");
  expect(content.findAvailable("beta", refusal) == nullptr &&
             refusal.find("Not provisioned") != std::string::npos,
         "pick of a disabled slug is refused with its reason");
  expect(content.findAvailable("nope", refusal) == nullptr &&
             refusal.find("unknown") != std::string::npos,
         "pick of an unknown slug is refused");

  // The panels are the AVAILABLE titles in catalog order: a title that cannot be started gets no
  // panel, so panel 0 is alpha and the two disabled entries are not panels at all.
  expect(content.panelCount() == 1, "one panel per available title");
  expect(content.panelContent().entries.size() == 1 && content.panelContent().entries[0].enabled,
         "every panel entry is selectable, because only startable titles have one");
  expect(content.panelTitle(0).identity->slug == "alpha", "panel 0 is the first available title");
  expect(content.panelOf("alpha") == 0, "a slug resolves to its panel");
  expect(content.panelOf("beta") == -1, "a title with no panel resolves to none");
  expect(content.panelOf("nope") == -1, "and so does an unknown slug");
  bool panelOutOfRange = false;
  try {
    (void)content.panelTitle(1);
  } catch (const std::out_of_range &) {
    panelOutOfRange = true;
  }
  expect(panelOutOfRange, "a panel outside the panels that exist is refused, not clamped");

  spyro::PickerRuntime runtime;
  std::FILE *out = std::tmpfile();
  expect(!runtime.handle("pick", "pick alpha", out), "an unbound runtime does not answer");
  runtime.bind(&content, nullptr);
  runtime.setSelection(0);
  expect(runtime.handle("pick", "pick alpha\n", out), "pick is handled");
  expect(runtime.takePick() == std::optional<std::string>("alpha"),
         "an accepted pick is taken once");
  expect(!runtime.takePick().has_value(), "and only once");
  runtime.handle("pick", "pick beta", out);
  expect(!runtime.takePick().has_value(), "a refused pick records nothing");

  // Selecting a panel is a different decision from starting one: it moves the highlight without
  // committing to a title.
  expect(runtime.handle("select", "select alpha", out), "select is handled");
  expect(runtime.takeSelection() == std::optional<int>(0), "a slug selects its panel");
  expect(!runtime.takeSelection().has_value(), "and only once");
  runtime.handle("select", "select beta", out);
  expect(!runtime.takeSelection().has_value(), "selecting a title with no panel records nothing");
  // With one panel there is nowhere to move: left and right both stay on it, and the panel count is
  // the content's own, so the channel cannot name a panel that does not exist.
  runtime.handle("select", "select right", out);
  expect(runtime.takeSelection() == std::optional<int>(0),
         "right on a one-panel picker stays on it");
  runtime.handle("select", "select left", out);
  expect(runtime.takeSelection() == std::optional<int>(0), "and so does left");
  expect(runtime.handle("picker", "picker shot", out), "picker shot is handled");
  expect(!runtime.takePick().has_value() && !runtime.takeSelection().has_value(),
         "and decides nothing");

  expect(!runtime.handle("frame", "frame", out), "other commands fall through to the framework");
  std::fclose(out);

  std::filesystem::remove_all(root);
  std::printf("title picker: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
