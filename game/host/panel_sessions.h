// panel_sessions.h — the picker's LIVE SESSIONS: one real title session per available title, and
// the rules about which of them runs.
//
// WHAT THIS OWNS, AND WHY IT IS NOT THE PICKER'S LOOP. Each panel is a whole boot-to-title machine,
// so three panels are three guests, three pads, three CD streams and three memory cards alive at
// once. The rules that keep that sane are one decision each, and they live here rather than in the
// picker's frame loop so that they can be stated once and obeyed by every route into the picker:
//
//   * ONE session advances. The selected panel runs its attract demo; the others hold the frame
//   they
//     last presented. A panel that is not selected does not advance, so it cannot be fast-forwarded
//     later — it is exactly where its demo left it.
//   * A session is booted the FIRST TIME IT IS NEEDED, and until it has presented a picture it gets
//     this frame's boot budget instead of the selected panel's step. That is how every panel
//     reaches a real picture as early as it can while still only ever running one guest at a time:
//     the selected title boots first (so the player sees something immediately), then the others,
//     one frame's budget each.
//   * Only the selected session is AUDIBLE, and only the confirmed session sees the PLAYER's pad.
//
// It owns the sessions; the panels' geometry is picker_layout's and the drawing is
// PickerComposite's.
#pragma once

#include "panel_logo.h"
#include "title_availability.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

class Core;
class GpuDevice;

namespace spyro {

class TitleSession;

class PanelSessions {
public:
  // One session per AVAILABLE title, in catalog order. `titles` is the whole probed catalog and is
  // NOT retained: the sessions hold references to its available entries, which outlive the picker.
  PanelSessions(std::span<const TitleAvailability> titles, GpuDevice &presentation);
  ~PanelSessions();
  PanelSessions(const PanelSessions &) = delete;
  PanelSessions &operator=(const PanelSessions &) = delete;

  int count() const {
    return static_cast<int>(sessions_.size());
  }
  // The title a panel shows. Panels are numbered by AVAILABLE title, so panel 0 is the first
  // provisioned and authenticated one in the catalog, whatever its slug.
  const TitleAvailability &title(int panel) const;
  // The panel showing this slug, or -1 when it is not one of the panels (unprovisioned,
  // unauthenticated).
  int panelOf(std::string_view slug) const;
  // The session behind a panel, or null. Null before boot() — which is also when it has no picture.
  TitleSession *session(int panel);
  // The same handle from a const host: the sessions are owned here and observed, not modified, by a
  // caller that only needs to ask what a panel is showing.
  const TitleSession *session(int panel) const;
  // The session behind a panel, if it has ever presented a picture.
  bool hasPicture(int panel) const;
  // The session behind a panel, if it has presented something VISIBLE. This is what the panels are
  // drawn from: a session that has only presented black boot frames has a panel that stays empty,
  // because an empty panel reads as "not loaded" while a black one reads as "broken".
  bool hasVisiblePicture(int panel) const;
  // Whether the panel may be DRAWN: its session has a picture in it. A panel that is not ready is
  // left empty, because an empty panel reads as "not chosen" and a black one reads as "broken".
  bool panelReady(int panel) const;
  // Whether the panel has finished pre-rolling: a picture past the boot prefix AND, where the title
  // can say so, its guest's own answer that the attract demo is playing. The old rule (a picture)
  // is what a title that has not measured its demo state falls back to.
  bool panelSettled(int panel) const;
  // The picture's own aspect, which is what decides how a panel is cover-cropped. 1:1 for a panel
  // with nothing in it, which the layout treats as the panel's own shape.
  int pictureWidth(int panel) const;
  int pictureHeight(int panel) const;
  // The Core a panel's session presents from, or null while it has none. What PickerComposite
  // draws.
  Core *core(int panel);
  // The title's OWN logo for this panel, decoded once from its own disc (panel_logo.h), or null
  // while it has not been read yet. It belongs to the panel: the same session that showed the logo
  // is the one that can produce it, and a logo cannot outlive the machine that decoded it.
  PanelLogo *logo(int panel);

  // The selected panel the player is on. Selecting is a decision about which demo the player is
  // watching and which one they can hear; WHICH session runs this frame is advance()'s business,
  // because a panel with no picture has to be brought up before the selected one can keep running.
  void select(int panel);
  int selected() const {
    return selected_;
  }

  // One picker frame. Runs the selected session's step, or spends `bootStepBudget` steps bringing
  // the next panel that has no picture yet up to one. Returns true when a guest advanced.
  bool advance(int bootStepBudget);
  // End every session except the one the player confirmed, and HAND THAT ONE OVER: the confirmed
  // session becomes the product (it owns the window and the player's pad again), and it outlives
  // this object, so it is moved out rather than released with the panels it was chosen from.
  std::unique_ptr<TitleSession> confirm();
  // Report what the sessions are doing, once per picker frame, at `lucent::debug`.
  void report() const;

private:
  // The panel to run this frame: the first with no picture (the selected one first among equals, so
  // the title the player is on appears before the others catch up), or the selected one when they
  // all have a picture.
  int nextPanelNeedingWork();
  // Read this panel's own logo out of that panel's own disc, once, on the first step the panel
  // runs. A no-op after the first success, and a no-op for a title whose logo has not been located.
  void takeLogo(int panel, TitleSession &session);

  std::vector<const TitleAvailability *> titles_;
  std::vector<std::unique_ptr<TitleSession>> sessions_;
  std::vector<bool> booted_;
  std::vector<std::unique_ptr<PanelLogo>> logos_;
  int selected_ = 0;
  int running_ = -1;
  // Where the boot rotation resumes: the panel after the last one worked, so panels that ALL need
  // work take turns instead of one of them being finished before the next is started.
  int mBootCursor_ = 0;
};

} // namespace spyro
