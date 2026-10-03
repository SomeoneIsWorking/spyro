#include "panel_sessions.h"

#include "panel_logo.h"
#include "spyro_runtime.h"
#include "title_availability.h"
#include "title_session.h"

#include "core.h"
#include "game_runtime.h"
#include "gpu_vk.h"

#include <lucent/log.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>

namespace spyro {
namespace {

// WHEN A PANEL LOOKS FOR ITS OWN LOGO. The logo is decoded out of the panel's own disc file, so
// there is no boot window to be inside: the disc is open from the session's first step and the
// bytes it holds do not change while the game runs. It is taken on the first step a panel runs,
// once, and never again — it is a GPU upload and a handful of sector reads, and a logo is not an
// animation. How long a title whose demo state is NOT MEASURED keeps pre-rolling before its panel
// settles. Spyro 1 and 2 reach their demos inside a few hundred fields of their own boot; this is
// comfortably past every card those discs draw, and bounded so a panel always appears.
constexpr int kUnmeasuredDemoSteps = 900;

} // namespace

PanelSessions::PanelSessions(std::span<const TitleAvailability> titles, GpuDevice &presentation) {
  for (const TitleAvailability &title : titles) {
    if (title.available()) {
      titles_.push_back(&title);
    }
  }
  sessions_.reserve(titles_.size());
  booted_.assign(titles_.size(), false);
  logos_.resize(titles_.size());
  for (const TitleAvailability *title : titles_) {
    // Constructed, not booted: a session that has not been asked for yet costs nothing but its
    // handle, and the boot is the expensive part (see advance).
    sessions_.push_back(std::make_unique<TitleSession>(*title, true, presentation));
  }
  if (sessions_.empty()) {
    lucent::warn("picker", "no provisioned title: the selector has no panels");
    return;
  }
  // The first panel is the one the player is on, and it is the one that boots first.
  select(0);
  lucent::info("picker", "{} title panel(s) over the probed catalog", count());
}

PanelSessions::~PanelSessions() = default;

const TitleAvailability &PanelSessions::title(int panel) const {
  if (panel < 0 || panel >= count()) {
    lucent::error("picker", "panel {} is outside the {} panel(s) that exist", panel, count());
    std::abort();
  }
  return *titles_[static_cast<std::size_t>(panel)];
}

int PanelSessions::panelOf(std::string_view slug) const {
  for (int panel = 0; panel < count(); ++panel) {
    if (titles_[static_cast<std::size_t>(panel)]->identity->slug == slug) {
      return panel;
    }
  }
  return -1;
}

TitleSession *PanelSessions::session(int panel) {
  if (panel < 0 || panel >= count()) {
    return nullptr;
  }
  return sessions_[static_cast<std::size_t>(panel)].get();
}

const TitleSession *PanelSessions::session(int panel) const {
  if (panel < 0 || panel >= count()) {
    return nullptr;
  }
  return sessions_[static_cast<std::size_t>(panel)].get();
}

bool PanelSessions::hasPicture(int panel) const {
  if (panel < 0 || panel >= count() || !booted_[static_cast<std::size_t>(panel)]) {
    return false;
  }
  return sessions_[static_cast<std::size_t>(panel)]->hasPicture();
}

// Whether the panel may be DRAWN: its title is past its own boot prefix and its session is holding
// a picture. Everything before this keeps its panel empty, because an empty panel reads as "not
// selected" while a black one reads as "broken". This is also what the composite asks, so there is
// one answer and not two that can drift apart.
bool PanelSessions::panelReady(int panel) const {
  if (panel < 0 || panel >= count() || !booted_[static_cast<std::size_t>(panel)]) {
    return false;
  }
  const TitleSession *session = this->session(panel);
  // Both, and in this order: the title must be past its boot prefix (its own driver says so) AND it
  // must have a frame with a picture in it (the probe says so). A publisher logo passes the second
  // and fails the first, which is the whole point of asking.
  if (session == nullptr || !session->showsTitlePicture()) {
    return false;
  }
  // A panel with nothing to show yet is looked at, which is what brings it up; a panel already
  // holding one is not, because its picture is whatever it last presented and asking again cannot
  // change the answer. This is the only place a session is probed outside the one that is running.
  if (!session->heldPicture()) {
    session->refreshPicture();
  }
  return session->heldPicture();
}

bool PanelSessions::hasVisiblePicture(int panel) const {
  return panelReady(panel);
}

int PanelSessions::pictureWidth(int panel) const {
  if (panel < 0 || panel >= count() || !booted_[static_cast<std::size_t>(panel)]) {
    return 1;
  }
  return sessions_[static_cast<std::size_t>(panel)]->pictureWidth();
}

int PanelSessions::pictureHeight(int panel) const {
  if (panel < 0 || panel >= count() || !booted_[static_cast<std::size_t>(panel)]) {
    return 1;
  }
  return sessions_[static_cast<std::size_t>(panel)]->pictureHeight();
}

Core *PanelSessions::core(int panel) {
  if (!hasVisiblePicture(panel)) {
    return nullptr;
  }
  return &sessions_[static_cast<std::size_t>(panel)]->core();
}

PanelLogo *PanelSessions::logo(int panel) {
  if (panel < 0 || panel >= count()) {
    return nullptr;
  }
  const std::unique_ptr<PanelLogo> &logo = logos_[static_cast<std::size_t>(panel)];
  return logo != nullptr ? logo.get() : nullptr;
}

void PanelSessions::takeLogo(int panel, TitleSession &session) {
  const std::size_t index = static_cast<std::size_t>(panel);
  if (index >= logos_.size() || logos_[index] != nullptr) {
    return; // already have it, or there is no such panel
  }
  if (session.steps() == 0) {
    return; // the panel has not run yet, so its disc has not been opened
  }
  const SpyroRuntime *runtime = dynamic_cast<const SpyroRuntime *>(session.core().runtime);
  if (runtime == nullptr) {
    return;
  }
  // The panel's logo is decoded out of the disc file this title's facts name, so there is no
  // artwork to wait for and nothing to compare against: the first successful decode is the logo,
  // and it is kept for the life of the panel.
  logos_[index] = extractPanelLogo(session.core(), runtime->logoFacts());
}

void PanelSessions::select(int panel) {
  if (panel < 0 || panel >= count() || panel == selected_) {
    return;
  }
  selected_ = panel;
  // The demo the player is looking at is the one they hear. Every other session releases the host
  // audio device, which is a per-process resource: three open streams would be three games arguing
  // over one output, and the one the player chose would not necessarily be the one that won it.
  for (int other = 0; other < count(); ++other) {
    if (TitleSession *session = this->session(other)) {
      session->setAudible(other == panel);
    }
  }
  lucent::info("picker", "selected panel {}: {}", panel, title(panel).identity->displayName);
}

// Whether the panel is DONE PRE-ROLLING: its title has a picture to show AND its guest says it is
// in its attract demo.
//
// This is the rule the panels used to get wrong, and the guest's own answer is what fixes it. A
// panel was frozen as soon as it had ANY picture past its boot prefix, and a title's publisher card
// is exactly that: Spyro 3's panel settled on the frame that says "Entering Demo Mode", which is
// not a demo, and stayed there for as long as the player looked at it.
//
// A title whose runtime has NOT measured its own demo state answers AttractState::Unknown, and then
// the older rule stands — a picture past the boot prefix is all this panel can know, so that is
// what it uses. An unmeasured title is never pre-rolled for ever and never claimed to be in a demo
// it cannot report.
bool PanelSessions::panelSettled(int panel) const {
  if (!panelReady(panel)) {
    return false;
  }
  const TitleSession *session = this->session(panel);
  if (session == nullptr) {
    return false;
  }
  Core &core = const_cast<TitleSession *>(session)->core();
  const SpyroRuntime *runtime = dynamic_cast<const SpyroRuntime *>(core.runtime);
  if (runtime == nullptr) {
    return true;
  }
  // The title answers a QUESTION about its own state; asking it does not change it, which is why
  // this may be asked of a panel that is not the one running.
  const AttractState state = runtime->attractState(*core.game, core);
  if (state != AttractState::Unknown) {
    return state == AttractState::InDemo;
  }
  // A TITLE THAT CANNOT ANSWER IS NOT A PANEL THAT IS ALREADY THERE. `Unknown` used to settle the
  // panel on its first picture, and a first picture is a publisher card: the panel came up grey and
  // frozen on the disc's Insomniac sign, which is the one thing an attract panel must never show.
  // So an unmeasured title pre-rolls PAST its cards and settles only once it has had the length of
  // boot its own measured siblings needed to reach a demo — bounded, because a panel that never
  // reaches one still has to appear rather than spin forever, and what it holds by then is a
  // running game, not a card.
  return session->steps() >= kUnmeasuredDemoSteps;
}

int PanelSessions::nextPanelNeedingWork() {
  // The SELECTED panel is the product: it runs, in colour, at the player's own speed. That is the
  // first answer and everything else is what the picker owes the panels the player is NOT looking
  // at.
  //
  // A panel the player is not looking at is PAUSED, and it is paused by not being stepped at all.
  // It holds the last frame it presented, which is the whole point: three games stepping at once is
  // three times the work for pictures the player cannot see properly, and a paused panel is a still
  // picture that costs nothing. The one exception is a panel that has never reached its own picture
  // — a panel cannot be shown frozen on its first black frame, and its title's opening cards leave
  // only by RUNNING, so an unsettled panel pre-rolls in the background until it has a picture to
  // hold.
  //
  // THE PRE-ROLLS BRING THEMSELVES UP TOGETHER, one slice each. Working a panel all the way to its
  // first picture before starting the next made the selector take as long as ALL THREE BOOTS ADDED
  // UP, and for most of that time two thirds of the screen was black — which reads as two titles
  // that failed rather than as three games starting. Rotating costs the same guest work and takes
  // about as long as the SLOWEST boot. One guest still runs at a time: this chooses which session
  // gets this picker's frame, it never runs two.
  //
  // A SETTLED panel is never chosen again until the selection moves onto it, at which point it
  // resumes from the frame it froze on. Nothing about a panel's resume is special: it is the same
  // session, with the same machine and the same guest, which was never stopped — only not asked to
  // step.
  // The selected panel first: it is the one the player is looking at, so it is the one that must be
  // showing something soonest, and a rotation that skipped it would leave the middle of the screen
  // empty while the two panels the player did not choose came up first.
  if (!panelSettled(selected_)) {
    return selected_;
  }
  // Then whoever is still pre-rolling, round-robin from where the last one left off.
  for (int turn = 0; turn < count(); ++turn) {
    const int panel = (mBootCursor_ + turn) % count();
    if (panel != selected_ && !panelSettled(panel)) {
      mBootCursor_ = (panel + 1) % count();
      return panel;
    }
  }
  return selected_;
}

bool PanelSessions::advance(int bootStepBudget) {
  const int panel = nextPanelNeedingWork();
  if (panel < 0 || count() == 0) {
    return false;
  }
  TitleSession *current = session(panel);
  if (current == nullptr) {
    return false;
  }
  // Exactly one session is running. Switching is pause-then-resume, never two at once: the frame a
  // panel gets to bring itself up is the frame the selected panel does not run.
  if (running_ != panel) {
    if (TitleSession *previous = session(running_)) {
      previous->pause();
    }
    current->resume();
    running_ = panel;
  }
  if (!booted_[static_cast<std::size_t>(panel)]) {
    if (!current->boot()) {
      lucent::error("picker",
                    "{} could not be booted: its panel stays empty",
                    title(panel).identity->displayName);
      booted_[static_cast<std::size_t>(panel)] = true;
      return false;
    }
    booted_[static_cast<std::size_t>(panel)] = true;
    lucent::info("picker", "booting panel {}: {}", panel, title(panel).identity->displayName);
  }
  if (current->returnRequested()) {
    return false;
  }
  // The selected panel and any panel still pre-rolling get the full BOOT BUDGET of steps per picker
  // frame: one is the product right now and the other is still starting up. A panel that has not
  // been chosen is never stepped at all (nextPanelNeedingWork never returns one), so this is the
  // only budget that exists. The picker stays responsive because the budget is bounded.
  const int budget = std::max(bootStepBudget, 1);
  bool advanced = false;
  for (int step = 0; step < budget; ++step) {
    if (current->end() != TitleSession::End::Running) {
      break;
    }
    current->step();
    advanced = true;
    // The panel that is RUNNING is the only one whose picture can have changed, so it is the only
    // one worth reading back: the probe replaces its held frame on its own slow cadence, which is
    // what keeps a demo playing in the panel instead of freezing on one frame of it.
    current->refreshPicture();
    // This panel's OWN logo, once: read out of this session's VRAM while its boot is still on the
    // screen that draws it, decoded, cached, and never asked for again.
    takeLogo(panel, *current);
    if (current->end() != TitleSession::End::Running) {
      lucent::info("picker",
                   "panel {} ({}) ended its run; its panel keeps the picture it last presented",
                   panel,
                   title(panel).identity->displayName);
      break;
    }
  }
  return advanced;
}

std::unique_ptr<TitleSession> PanelSessions::confirm() {
  if (count() == 0) {
    return nullptr;
  }
  const int chosen = selected_;
  // Every other session is destroyed here — their machines, pads, CD streams and cards die with
  // them, which is the whole teardown and the reason a second title in one process starts clean.
  for (int panel = 0; panel < count(); ++panel) {
    if (panel != chosen) {
      sessions_[static_cast<std::size_t>(panel)].reset();
      booted_[static_cast<std::size_t>(panel)] = false;
    }
  }
  std::unique_ptr<TitleSession> session = std::move(sessions_[static_cast<std::size_t>(chosen)]);
  if (session != nullptr) {
    // The chosen session becomes the product: it owns the window again and it alone sees the
    // player's pad, which is how the confirm press reaches the game the player just chose.
    session->presentToWindow();
    session->setPlayerInput(true);
    session->setAudible(true);
  }
  lucent::info("picker",
               "confirmed panel {}: {} continues into the window",
               chosen,
               title(chosen).identity->displayName);
  return session;
}

void PanelSessions::report() const {
  std::string line = "panels:";
  for (int panel = 0; panel < count(); ++panel) {
    const bool isSelected = panel == selected_;
    line += std::format(" [{}]{}{} {}",
                        panel,
                        isSelected ? "*" : " ",
                        titles_[static_cast<std::size_t>(panel)]->identity->slug,
                        panelReady(panel)
                            ? (panelSettled(panel) ? "demo" : "picture")
                            : (booted_[static_cast<std::size_t>(panel)] ? "booting" : "cold"));
  }
  lucent::debug("picker", "{}", line);
}

} // namespace spyro
