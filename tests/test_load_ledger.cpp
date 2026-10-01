// LoadLedger — the record, its coverage denominator, and the answer a withheld completion gives.
//
// The positive leg is a stage machine that completes every operation in the same field it issues
// it, which is what the port's CD overrides are supposed to produce. The negative leg is the SAME
// stage machine with the completion withheld, and the assertion is not that the report looks
// different but that the machine stops: it issues one operation, never reaches the next stage, and
// the ledger says so with a pending count and a latency that is deliberately not a small number.
// Without that leg a report of "0 fields" would be consistent with a stage machine that never ran.

#include "load_ledger.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <lucent/log.h>

namespace {

using spyro::load_ledger::Ledger;
using spyro::load_ledger::Operation;

void require(bool condition, const char *message) {
  if (!condition) {
    lucent::error("load-ledger-test", "{}", message);
    std::exit(1);
  }
}

// The guest's own shape, as 0155 section 2.3 measured it: a stage machine calls its loader once
// per field, the loader issues at most one read, and the stage advances only when that read has
// completed. `withholdCompletion` is the fault: the guest never calls the retry step that
// dispatches 0x80016490, which is exactly what a withheld completion latch looks like from inside.
void runStageMachine(Ledger &ledger, bool withholdCompletion) {
  constexpr std::uint32_t kFinalStage = 13;
  constexpr std::uint32_t kFirstStreamingStage = 2;
  std::uint32_t stage = 0;
  std::uint64_t field = 0;
  for (; field < 200; ++field) {
    if (stage < kFinalStage) {
      // A blocking issuer first (the census's S group), then the streaming stage machine.
      const bool blocking = stage < kFirstStreamingStage;
      const auto index = ledger.begin({.issuerSite = blocking ? 0x8001253Cu : 0x8001569Cu,
                                       .baseLba = 37u,
                                       .byteOffset = 0u,
                                       .length = blocking ? 2048u : 81920u,
                                       .destination = 0x801C0000u,
                                       .deferred = !blocking,
                                       .accepted = true,
                                       .musicGateClearAtIssue = true,
                                       .fieldCounted = true,
                                       .fieldAtIssue = field,
                                       .loadStageAtIssue = static_cast<std::int32_t>(stage)});
      ledger.attachDigest(index, std::string(64, 'a'));
      if (blocking) {
        ledger.completeAtIssue(index);
        stage = kFirstStreamingStage;
      } else if (withholdCompletion) {
        // The latch is never taken: the guest spins here forever, exactly as retail's Loop B
        // would without the completion callback.
        return;
      } else {
        ledger.completePending(field, static_cast<std::int32_t>(stage));
        ++stage;
      }
    }
  }
}

void checkCoverageDenominator() {
  Ledger ledger;
  runStageMachine(ledger, false);
  const std::string report = ledger.report();
  // One blocking read, then one streaming read per stage from 2 to 12, and the machine reaches
  // stage 13: twelve operations in all.
  require(ledger.operations().size() == 12,
          "the stage machine must reach stage 13 with one operation per stage");
  for (const Operation &operation : ledger.operations()) {
    require(operation.completed, "every operation of a healthy run completes");
    require(operation.latencyFields() == 0u,
            "a read the override completes in the issuing field costs no fields");
  }
  require(ledger.pendingCount() == 0u, "a healthy run leaves no operation pending");
  require(report.find("issuer sites exercised 2 of 31") != std::string::npos,
          "the coverage denominator must divide by the 31 named sites, not by what ran");
  require(report.find("A01@0x8001569C UNREACHED") == std::string::npos,
          "A01 is reached by the machine and must not be listed as unreached");
  require(report.find("S09@0x8002EEC4 UNREACHED") != std::string::npos,
          "a site the route never reaches must be named in the report");
  require(ledger.maxLatencyFields() == 0u, "the worst measured latency must be reported");
}

void checkWithheldCompletionStallsTheStageMachine() {
  Ledger ledger;
  runStageMachine(ledger, true);
  // One blocking read, then the first streaming read, which never completes. The stage machine
  // returns rather than advancing, so the ledger holds exactly two operations and one of them is
  // pending: the stall is visible as state, not as an absence of output.
  require(ledger.operations().size() == 2,
          "a withheld completion must leave the stage machine with two operations, not thirteen");
  const Operation &stalled = ledger.operations().back();
  require(!stalled.completed, "the stalled operation must not be reported complete");
  require(stalled.latencyFields() == spyro::load_ledger::kPendingFields,
          "an unfinished operation must report no latency, not zero");
  require(ledger.pendingCount() == 1u, "the withheld operation must be counted as pending");
  const std::string report = ledger.report();
  require(report.find("still pending 1") != std::string::npos,
          "the run-end report must carry the pending count");
  require(report.find("pending") != std::string::npos,
          "the per-operation table must mark the unfinished operation");
}

void checkMeasuredLatency() {
  Ledger ledger;
  const auto index = ledger.begin({.issuerSite = 0x8001569Cu,
                                   .baseLba = 37u,
                                   .length = 65536u,
                                   .destination = 0x801C0000u,
                                   .deferred = true,
                                   .accepted = true,
                                   .musicGateClearAtIssue = false,
                                   .fieldCounted = true,
                                   .fieldAtIssue = 120u,
                                   .loadStageAtIssue = 4});
  require(ledger.pendingCount() == 1u, "an accepted deferred read waits for its completion");
  ledger.completePending(123u, 4);
  require(ledger.operations()[index].latencyFields() == 3u,
          "latency is the fields between issue and completion");
  require(ledger.maxLatencyFields() == 3u, "the worst latency must be the maximum over operations");
  require(ledger.operations()[index].loadStageAtCompletion == 4,
          "the completion records the stage the guest was in");
  require(!ledger.operations()[index].musicGateClearAtIssue,
          "the XA readiness bit is recorded as it was read at issue");
}

void checkRefusalStaysVisible() {
  Ledger ledger;
  const auto index = ledger.begin({.issuerSite = 0x8002EEC4u,
                                   .baseLba = 37u,
                                   .length = 40960u,
                                   .destination = 0x801C0000u,
                                   .deferred = true,
                                   .accepted = true,
                                   .fieldCounted = true,
                                   .fieldAtIssue = 7u,
                                   .loadStageAtIssue = 2});
  ledger.markRefused(index);
  require(ledger.operations()[index].accepted == false, "a refused read is not accepted");
  require(ledger.pendingCount() == 0u, "a refused read cannot still be waiting for a completion");
  require(ledger.report().find("REFUSED") != std::string::npos,
          "a refusal must stay in the record: a route that failed and a route that never ran are "
          "different reports");
}

void checkOperationsBeforeAFieldCounterExist() {
  Ledger ledger;
  ledger.begin({.issuerSite = 0x8001253Cu,
                .baseLba = 37u,
                .length = 2048u,
                .destination = 0x80020000u,
                .fieldCounted = false});
  require(ledger.maxLatencyFields() == 0u,
          "an operation with no field counter is excluded from the latency maximum");
  require(ledger.report().find("before a field owner existed 1") != std::string::npos,
          "operations issued before the field owner existed are counted and reported");
}

void checkSiteTable() {
  require(spyro::load_ledger::knownIssuerSiteCount() == 31,
          "the census names 31 issuer sites and the denominator must be that many");
  const auto *site = spyro::load_ledger::findIssuerSite(0x80016500u);
  require(site == nullptr, "a load function is not an issuer site");
  const auto *named = spyro::load_ledger::findIssuerSite(0x8001569Cu);
  require(named != nullptr && std::string(named->id) == "A01",
          "the streaming LoadLevel stage 2 site must resolve to A01");
  require(named != nullptr && !named->blocking, "A01 is a streaming issuer");
  const auto *blocking = spyro::load_ledger::findIssuerSite(0x8001253Cu);
  require(blocking != nullptr && blocking->blocking, "S01 is a blocking issuer");
}

} // namespace

int main() {
  checkSiteTable();
  checkCoverageDenominator();
  checkWithheldCompletionStallsTheStageMachine();
  checkMeasuredLatency();
  checkRefusalStaysVisible();
  checkOperationsBeforeAFieldCounterExist();
  return 0;
}
