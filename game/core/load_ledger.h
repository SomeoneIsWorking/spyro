#pragma once

// LoadLedger — one entry per Spyro CD read operation, and the residual-latency instrument.
//
// WHAT THIS IS FOR. Issue 0155 §6 established that the port's CD overrides remove storage latency
// at the primitive level, and §7 named three measurements that were impossible without a record of
// what the overrides actually did: M1 (the payload of every operation, compared against retail),
// M2 (terminal state) and M3 (the fields each operation still costs, per operation and per load
// stage). None of the three could be answered by a counter, because a counter that reads zero
// looks exactly like a clean measurement of absence. The ledger is therefore a RECORD, not a
// tally: it names the issuer site, the range, the destination, the payload's content identity and
// the two fields the operation spans, so a run can be compared with another run operation by
// operation.
//
// DIAGNOSTICS ONLY. Every entry is filled from state the run has already produced. Nothing here
// reads a completion to decide behaviour, writes a guest word, or takes part in scheduling; the
// only consumer of a pending operation is the report. That is what makes the withholding test
// meaningful: the ledger can report a stage machine stalling without being able to cause one.
//
// OWNERSHIP. One Ledger per Core, held by spyro::Context next to the ArchiveTransfer whose reads it
// records, and fed by the one place that issues them (cd_queue.cpp). No globals, no singletons and
// no function-local statics: two Cores in one process (the title selector runs one at a time but
// nothing in the type system says so) keep two separate ledgers, and a second Core's run cannot
// add an operation to the first's report.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spyro::load_ledger {

// One named CD read issuer site from the closed census in 0155 §2. The set is closed and named on
// purpose: a report that divided by "the operations seen" would make a route that never dies and
// never pauses look like full coverage, so the denominator is the 31 sites the census enumerates
// and every site the run did NOT reach is printed by name.
struct IssuerSite {
  std::uint32_t address;
  const char *id;
  const char *role;
  // Which of the two overrides owns the site: the blocking loader (0x80016500) or the streaming
  // loader (0x80016698). T01 is the title overlay's site and is reached through the same override.
  bool blocking;
};

// The census, in the issue's order. `knownIssuerSiteCount()` is the denominator.
const IssuerSite *knownIssuerSites();
std::size_t knownIssuerSiteCount();
// The site's row, or nullptr when the address is outside the census. A call the census did not
// name is a finding, not a reason to refuse: the report counts it and says so.
const IssuerSite *findIssuerSite(std::uint32_t address);

// One operation, from the override's entry to its completion.
//
// `latencyFields()` is the M3 measurement: the fields between the operation being issued and its
// completion. It is 0 for a blocking issuer (the payload is in guest RAM before the override
// returns) and the number of fields the guest waited for the deferred completion otherwise. An
// operation that has not completed reports `kPendingFields`, which is deliberately not a small
// number: an unfinished operation has no measured latency, and a report that printed 0 for it
// would claim the removal is free.
inline constexpr std::uint64_t kPendingFields = ~0ull;

struct Operation {
  std::uint32_t issuerSite = 0;
  // The guest PC the override was entered with, recorded next to `issuerSite` because `$ra - 8`
  // only names a `jal` site when the CALLER was guest code. The port's own native boot owner
  // dispatches the loader directly (BootSequence::loadAssets), and then `$ra` is a host return
  // address; printing both columns is what makes that visible instead of leaving a plausible
  // looking guest address in the site column.
  std::uint32_t guestPc = 0;
  std::uint32_t baseLba = 0;
  std::uint32_t byteOffset = 0;
  std::uint32_t length = 0;
  std::uint32_t destination = 0;
  // The SHA-256 hex of the payload, taken from the digest image_publication already computed for
  // the image identity. The ledger never hashes anything itself: a second digest over the same
  // bytes would be a second implementation of the same fact that could disagree with the first.
  std::string digest;
  bool deferred = false;
  bool accepted = false;
  // Whether [0x800774B4] & 0x40 — the CD music command queue's idle bit, which every cooperative
  // loader tests before it will advance a stage — was clear when the operation was issued. This
  // is the split M3 asks for, and it is read once, at issue.
  bool musicGateClearAtIssue = false;
  // False when no field owner was published yet, in which case both field numbers are 0 and the
  // operation is excluded from the latency denominator rather than counted as a free operation.
  bool fieldCounted = false;
  std::uint64_t fieldAtIssue = 0;
  std::uint64_t fieldAtCompletion = 0;
  std::int32_t loadStageAtIssue = 0;
  std::int32_t loadStageAtCompletion = 0;
  bool completed = false;

  constexpr std::uint64_t latencyFields() const {
    return completed ? fieldAtCompletion - fieldAtIssue : kPendingFields;
  }
};

// Accumulates across a run. A plain member of spyro::Context; the type has no Core, no disc and no
// ambient dependency, so a test can drive it without a guest.
class Ledger {
public:
  // Record an operation at issue. Returns its index. A deferred operation that has not completed
  // when the next one is issued is a guest that moved on without its completion, and the ledger
  // says so rather than silently completing it at the next issue.
  std::size_t begin(const Operation &issue);
  // Attach the payload's content identity. Separate from begin() because the digest exists only
  // after the transfer has read every sector, and because a refused operation has none.
  void attachDigest(std::size_t index, std::string digestHex);
  // The transfer owner refused the read before any RAM write. The operation stays in the ledger:
  // a refusal that vanished would make "the route never issued this" and "the route issued this
  // and it failed" the same number.
  void markRefused(std::size_t index);
  // A blocking issuer is complete when the override returns, which is the same field it was
  // issued in. Recording it through this call rather than at begin() keeps "completed" a fact
  // about the operation rather than an assumption about which override was entered.
  void completeAtIssue(std::size_t index);
  // The guest's own completion callback ran. Completes the operation that is pending, if any.
  void completePending(std::uint64_t field, std::int32_t loadStage);

  const std::vector<Operation> &operations() const {
    return operations_;
  }
  bool empty() const {
    return operations_.empty();
  }
  // Operations issued and never completed. Zero on a healthy run; the number a withheld
  // completion produces.
  std::size_t pendingCount() const;
  // The largest measured latency over operations that completed inside a counted field, and how
  // many operations it covers.
  std::uint64_t maxLatencyFields() const;
  // One report: every operation, then the denominators. A report with no operations says so in
  // words rather than printing an empty table that reads like a run with no CD traffic.
  std::string report() const;
  // The run-end summary, through Lucent. Always logged: it is a handful of lines and it is the
  // only place the coverage denominator appears. The per-operation table is logged too, because
  // M3's measurement IS the table.
  void logSummary() const;

private:
  std::vector<Operation> operations_{};
  std::size_t pending_ = 0;
  bool pendingValid_ = false;
};

// Writes `report()` to the path named by PSXPORT_LOAD_LEDGER, read through the framework's
// configuration owner: the per-operation table is many lines and the logger emits one line per
// call, so it goes to a file the run's own evidence directory already owns. A run without the
// variable writes no file and pays one cached lookup at run end.
void writeReportIfRequested(const Ledger &ledger);

} // namespace spyro::load_ledger
