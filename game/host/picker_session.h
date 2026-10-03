#pragma once

#include "picker_composite.h"
#include "picker_content.h"
#include "picker_layout.h"
#include "picker_runtime.h"

#include "gpu_vk_device.h"

#include <memory>
#include <span>
#include <vector>

class Core;
class Game;
class GpuDevice;

namespace spyro {

class PanelSessions;
class TitleSession;

// The title selector: the product state in which SEVERAL games are alive at once, each playing its
// own attract demo in its own panel.
//
// WHAT IT OWNS, AND WHAT IT DOES NOT. It owns the frame: the host's own Game (which holds the
// control endpoint, the pad the player is read through and the screen text), the panel sessions,
// and one composed frame per picker frame. It does not own the geometry (PickerLayout), the
// sessions (PanelSessions) or the drawing (PickerComposite) — each is one decision, and keeping
// them apart is what lets the geometry be tested without a GPU and the sessions be reasoned about
// without a screen.
//
// THE CONTRACT IT KEEPS. Exactly one guest advances per frame, only the selected session is heard,
// no panel's guest sees the player's pad, a panel with no picture draws nothing, and confirming
// hands the SELECTED session — the same one that was playing its demo — to the caller, which runs
// it full-window from exactly where it was. Every other session is destroyed on the way out.
//
// `presentation` (the process's window and device) must outlive this session.
class PickerSession {
public:
  enum class Outcome : std::uint8_t { Chosen, Ended };
  struct Result {
    Outcome outcome = Outcome::Ended;
    std::unique_ptr<TitleSession> session; // the chosen title's LIVE session, set when Chosen
  };

  // `titles` is the probed catalog the panels are built from (one panel per AVAILABLE entry, in
  // catalog order) and `content` its content; both outlive this session. `frameCap` of 0 runs until
  // a title is chosen, otherwise the screen ends after that many frames.
  PickerSession(PickerRuntime &runtime,
                std::span<const TitleAvailability> titles,
                const PickerContent &content,
                int frameCap,
                GpuDevice &presentation);
  // Out of line because the result OWNS a session: the picker hands the chosen title's live session
  // to its caller, and a unique_ptr over a forward-declared type can only be destroyed where that
  // type is complete.
  ~PickerSession();

  Result run();

private:
  // Read the pad and the control channel for one frame and act on them. Returns the chosen panel,
  // or -1 to keep the selector up.
  int serviceInput(Game &host);

  PickerRuntime &runtime_;
  std::span<const TitleAvailability> titles_;
  const PickerContent &content_;
  int frameCap_;
  GpuDevice &presentation_;
  PickerLayout layout_;
  std::unique_ptr<PickerComposite> composite_;
  std::unique_ptr<PanelSessions> panels_;
  std::vector<PanelSource> sources_;
  // The active-low buttons that confirmed, handed to the chosen game so the press reaches it.
  std::uint16_t confirmMask_ = 0xFFFFu;
};

} // namespace spyro
