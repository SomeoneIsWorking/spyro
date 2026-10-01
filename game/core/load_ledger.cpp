#include "load_ledger.h"

#include "cfg.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include <lucent/log.h>

namespace spyro::load_ledger {
namespace {

// The closed census of 0155 §2, in the issue's order: eleven blocking issuers (S), nineteen
// streaming issuers in the main image (A) and one in the title overlay (T). Every address here was
// re-derived from `SCUS_942.28` in this worktree and agrees with the issue's own list; the method
// note in that issue records the instruction-field form the scan used, because the plain
// PC-relative reading of these words finds nothing at all.
constexpr std::array<IssuerSite, 31> kSites{{
    {0x8001253Cu, "S01", "WadInitialize: WAD header", true},
    {0x80012924u, "S02", "boot: title cutscene header", true},
    {0x80012970u, "S03", "boot: title VRAM", true},
    {0x80012994u, "S04", "boot: title overlay", true},
    {0x800129C0u, "S05", "boot: Universal logo image", true},
    {0x8005B83Cu, "S06", "PETE.WAD", true},
    {0x8002D31Cu, "S07", "credits overlay", true},
    {0x8002E328u, "S08", "pause menu: quit to title", true},
    {0x8002EEC4u, "S09", "game over skybox", true},
    {0x80033688u, "S10", "demo end: title overlay", true},
    {0x80014518u, "S11", "respawn: current level scene", true},
    {0x8001569Cu, "A01", "LoadLevel 2: level overlay", false},
    {0x800156E4u, "A02", "LoadLevel 3: level header", false},
    {0x80015750u, "A03", "LoadLevel 4: level VRAM", false},
    {0x8001582Cu, "A04", "LoadLevel 5: SPU data", false},
    {0x800158C8u, "A05", "LoadLevel 8: level data", false},
    {0x80015A3Cu, "A06", "LoadLevel 9: model data", false},
    {0x80015BC0u, "A07", "LoadLevel 11: level scene", false},
    {0x80014608u, "A08", "LoadCutscene 0: cutscene header", false},
    {0x80014680u, "A09", "LoadCutscene 1: cutscene VRAM", false},
    {0x80014740u, "A10", "LoadCutscene 3: SRAM", false},
    {0x800147C8u, "A11", "LoadCutscene 4: cutscene data", false},
    {0x800148ACu, "A12", "LoadCutscene 5: model", false},
    {0x80014920u, "A13", "LoadCutscene 7: model tail", false},
    {0x80014A08u, "A14", "LoadCutscene 8: scene", false},
    {0x80014CCCu, "A15", "LoadDragonCutscene 0", false},
    {0x80014E6Cu, "A16", "LoadDragonCutscene 1", false},
    {0x80015188u, "A17", "LoadDragonCutscene 2", false},
    {0x80015248u, "A18", "LoadDragonCutscene 2: overflow block", false},
    {0x8003381Cu, "A19", "demo end: wad1 title graphics", false},
    {0x8007ADFCu, "T01", "title overlay TSM_Init: wad1", false},
}};

std::string format(std::uint64_t value) {
  return std::to_string(value);
}

std::string hex(std::uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08X", value);
  return text;
}

std::string siteLabel(std::uint32_t address) {
  const IssuerSite *site = findIssuerSite(address);
  return site != nullptr ? std::string(site->id) + " " + hex(address)
                         : hex(address) + " (outside the 0155 census)";
}

} // namespace

const IssuerSite *knownIssuerSites() {
  return kSites.data();
}

std::size_t knownIssuerSiteCount() {
  return kSites.size();
}

const IssuerSite *findIssuerSite(std::uint32_t address) {
  const auto *it = std::ranges::find_if(kSites, [address](const IssuerSite &site) {
    return site.address == address;
  });
  return it == kSites.end() ? nullptr : &*it;
}

std::size_t Ledger::begin(const Operation &issue) {
  operations_.push_back(issue);
  if (issue.deferred && issue.accepted) {
    // A deferred read that the transfer accepted is waiting for the guest's own completion
    // callback, so it is the one operation a later completion can close.
    pending_ = operations_.size() - 1;
    pendingValid_ = true;
  }
  return operations_.size() - 1;
}

void Ledger::attachDigest(std::size_t index, std::string digestHex) {
  if (index < operations_.size()) {
    operations_[index].digest = std::move(digestHex);
  }
}

void Ledger::markRefused(std::size_t index) {
  if (index >= operations_.size()) {
    return;
  }
  operations_[index].accepted = false;
  // A refused operation cannot complete: the transfer owner exits the guest before any RAM write.
  if (pendingValid_ && pending_ == index) {
    pendingValid_ = false;
  }
}

void Ledger::completeAtIssue(std::size_t index) {
  if (index >= operations_.size()) {
    return;
  }
  Operation &operation = operations_[index];
  operation.completed = true;
  operation.fieldAtCompletion = operation.fieldAtIssue;
  operation.loadStageAtCompletion = operation.loadStageAtIssue;
  if (pendingValid_ && pending_ == index) {
    pendingValid_ = false;
  }
}

void Ledger::completePending(std::uint64_t field, std::int32_t loadStage) {
  if (!pendingValid_) {
    return;
  }
  Operation &operation = operations_[pending_];
  operation.completed = true;
  operation.fieldAtCompletion = field;
  operation.loadStageAtCompletion = loadStage;
  pendingValid_ = false;
}

std::size_t Ledger::pendingCount() const {
  std::size_t count = 0;
  for (const Operation &operation : operations_) {
    if (operation.deferred && operation.accepted && !operation.completed) {
      ++count;
    }
  }
  return count;
}

std::uint64_t Ledger::maxLatencyFields() const {
  std::uint64_t worst = 0;
  for (const Operation &operation : operations_) {
    if (operation.completed && operation.fieldCounted) {
      worst = std::max(worst, operation.latencyFields());
    }
  }
  return worst;
}

std::string Ledger::report() const {
  std::string out;
  if (operations_.empty()) {
    out +=
        "load ledger: 0 operations seen; no CD read was issued in this run, so the coverage "
        "denominator below is reported against an empty run and every named site is unreached.\n";
  }
  out += "load ledger: per operation\n";
  // The byte offset and the FULL digest are here because this table is M1's input record (0155 §7):
  // the payload multiset it compares is (issuer site, LBA, byte offset, length, destination,
  // SHA-256), so a column that carried a 16-character digest prefix or no offset at all would be a
  // comparison of a prefix and a guess. The report goes to a file, so the length costs nothing.
  out += "  # site ra-8 pc lba off len dest deferred stage@issue field@issue field@done latency "
         "music-idle@issue sha256\n";
  for (std::size_t index = 0; index < operations_.size(); ++index) {
    const Operation &operation = operations_[index];
    out += "  " + format(index) + " " + siteLabel(operation.issuerSite) + " " +
           hex(operation.guestPc) + " " + hex(operation.baseLba) + " " + hex(operation.byteOffset) +
           " " + format(operation.length) + " " + hex(operation.destination) + " " +
           (operation.deferred ? "stream" : "blocking") + " " +
           format(static_cast<std::uint64_t>(operation.loadStageAtIssue)) + " " +
           (operation.fieldCounted ? format(operation.fieldAtIssue) : std::string("nocounter")) +
           " " +
           (operation.completed && operation.fieldCounted
                ? format(operation.fieldAtCompletion)
                : (operation.completed ? std::string("nocounter") : std::string("pending"))) +
           " " +
           (operation.completed
                ? format(operation.latencyFields())
                : (operation.fieldCounted ? std::string("pending") : std::string("nocounter"))) +
           " " + (operation.musicGateClearAtIssue ? "clear" : "busy") + " " +
           (operation.digest.empty() ? std::string("NO-DIGEST") : operation.digest) +
           (operation.accepted ? "" : " REFUSED") + "\n";
  }

  // The coverage denominator. A site the run never reached is named, so "the route never died"
  // and "the route died and the census is complete" are different reports.
  std::string reached;
  std::string unreached;
  std::size_t outsideCensus = 0;
  for (const IssuerSite &site : kSites) {
    const auto used = std::ranges::any_of(operations_, [&site](const Operation &operation) {
      return operation.issuerSite == site.address;
    });
    const std::string label =
        std::string(site.id) + "@" + hex(site.address) + (used ? "" : " UNREACHED");
    if (used) {
      reached += (reached.empty() ? "" : ", ") + label;
    } else {
      unreached += (unreached.empty() ? "" : ", ") + label;
    }
  }
  for (const Operation &operation : operations_) {
    if (findIssuerSite(operation.issuerSite) == nullptr) {
      ++outsideCensus;
    }
  }
  std::size_t exercised = 0;
  for (const IssuerSite &site : kSites) {
    const auto used = std::ranges::any_of(operations_, [&site](const Operation &operation) {
      return operation.issuerSite == site.address;
    });
    exercised += used ? 1u : 0u;
  }
  out += "load ledger: issuer sites exercised " + format(exercised) + " of " +
         format(kSites.size()) + " named in 0155 section 2" +
         (outsideCensus == 0 ? ""
                             : ("; " + format(outsideCensus) +
                                " operation(s) came from an address the census does not name")) +
         "\n";
  out += "load ledger: exercised sites (" + format(exercised) +
         "): " + (reached.empty() ? std::string("none") : reached) + "\n";
  out += "load ledger: unreached sites (" + format(kSites.size() - exercised) +
         "): " + (unreached.empty() ? std::string("none") : unreached) + "\n";
  out += "load ledger: operations seen " + format(operations_.size()) + ", still pending " +
         format(pendingCount()) + ", worst measured latency " + format(maxLatencyFields()) +
         " fields, operations issued before a field owner existed " +
         format(static_cast<std::uint64_t>(std::ranges::count_if(operations_,
                                                                 [](const Operation &operation) {
                                                                   return !operation.fieldCounted;
                                                                 }))) +
         "\n";
  return out;
}

void Ledger::logSummary() const {
  std::size_t exercised = 0;
  std::string unreached;
  for (const IssuerSite &site : kSites) {
    const auto used = std::ranges::any_of(operations_, [&site](const Operation &operation) {
      return operation.issuerSite == site.address;
    });
    if (used) {
      ++exercised;
    } else {
      unreached += (unreached.empty() ? "" : ", ") + std::string(site.id) + "@" + hex(site.address);
    }
  }
  const auto blocking =
      static_cast<std::uint64_t>(std::ranges::count_if(operations_, [](const Operation &entry) {
        return !entry.deferred;
      }));
  const auto streaming = operations_.size() - blocking;
  std::size_t outsideCensus = 0;
  for (const Operation &operation : operations_) {
    if (findIssuerSite(operation.issuerSite) == nullptr) {
      ++outsideCensus;
    }
  }
  lucent::info("load-ledger",
               "operations={} blocking={} streaming={} pending={} worst_latency_fields={} "
               "issuer_sites={}/{} unnamed_issuer={} unreached=[{}]",
               operations_.size(),
               blocking,
               streaming,
               pendingCount(),
               maxLatencyFields(),
               exercised,
               kSites.size(),
               outsideCensus,
               unreached);
}

void writeReportIfRequested(const Ledger &ledger) {
  const char *path = cfg_str("PSXPORT_LOAD_LEDGER");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  FILE *file = std::fopen(path, "wb");
  if (file == nullptr) {
    lucent::error("load-ledger", "cannot open {} for the report", path);
    return;
  }
  const std::string text = ledger.report();
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
}

} // namespace spyro::load_ledger
