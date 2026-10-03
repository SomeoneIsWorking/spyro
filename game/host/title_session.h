#pragma once

#include "title_availability.h"

#include "gpu_vk_device.h"

#include <cstdint>
#include <memory>

class Core;
class Game;

namespace spyro {

// One boot-to-exit run of one title, as a STEPPABLE owner: `boot()` brings the Game and the guest
// up, `step()` advances the product by one step, and destruction is the whole teardown — so the
// next session starts from a process with nothing of this one left in it. The window and the
// presentation device are the PRODUCT's (`presentation`), so they survive every session; nothing
// session-owned reaches that object.
//
// It is steppable rather than a `run()` because the picker needs its titles to be ALIVE at the same
// time: three sessions, one advancing and two paused, all showing their own picture. A loop that
// owns itself cannot be paused, and a title that is destroyed and rebuilt to show a panel would
// restart from power-on every time the player moved the selection.
//
// `presentation` must outlive this session.
class TitleSession {
public:
  enum class End : std::uint8_t {
    Running,            // still stepping: no end has been reached
    ReturnedToSelector, // the player (or `session return`) asked for the title selector
    Finished,           // the frame cap was reached or the run ended itself
    Refused,            // the executable could not be loaded
  };

  TitleSession(const TitleAvailability &title, bool selectorAvailable, GpuDevice &presentation);
  ~TitleSession();
  TitleSession(const TitleSession &) = delete;
  TitleSession &operator=(const TitleSession &) = delete;

  // Construct the Game, boot the authenticated executable and run the init prefix. False when the
  // executable could not be loaded; a session that failed to boot has no picture and never will.
  bool boot();
  // One product step. A no-op before boot(), and the caller's business to make at most one per
  // frame.
  void step();
  // Why the session stopped. `Finished` until it is asked to return to the selector.
  End end() const {
    return end_;
  }
  bool returnRequested() const;

  Core &core();
  // The title this session is running. What the host logs when the picker hands one over.
  const TitleAvailability &title() const {
    return title_;
  }
  std::uint64_t steps() const {
    return steps_;
  }
  // Whether this session has presented a picture yet. A panel with no picture must show nothing at
  // all rather than an empty frame that looks like a title that failed.
  // Whether this title is showing the picture a panel should advertise: past its retail boot
  // prefix, and drawing its own scene rather than a card the guest uploaded. Both answers are the
  // TITLE's — its frame driver and its presentation owner — because no pixel test can tell an
  // animated publisher logo from an attract demo.
  bool showsTitlePicture() const;
  bool hasPicture() const;
  // Whether the picture this session last presented actually shows something. A cold boot presents
  // black for its first frames, and a panel that stops there is indistinguishable from a title that
  // did not start — so the picker's panels stay EMPTY until their session has content, and the boot
  // budget keeps spending steps until it does.
  //
  // The answer is about the LAST presented frame, so it is only asked of a session that is still
  // coming up: each ask is a GPU readback, which is why the picker asks once every few steps rather
  // than per frame.
  // Whether this session is HOLDING a picture, and the act of looking for one. They are separate
  // because looking costs a GPU readback and holding costs nothing: a panel that is paused holds
  // the frame it froze on and must not be asked to look again, or the selector spends its time
  // reading back pictures that cannot have changed. `refreshPicture()` is for a session that is
  // RUNNING (or has nothing yet); `heldPicture()` is the answer, whenever it is wanted.
  bool heldPicture() const {
    return contentSeen_;
  }
  void refreshPicture() const;
  // The picture's own aspect, for the panel that will draw it. A session that has presented nothing
  // reports 1:1, which the layout treats as "the panel's own shape".
  int pictureWidth() const;
  int pictureHeight() const;
  // The image this session's panel draws: the newest presented frame that HAD a picture, so a fade
  // or load screen between the guest's scenes leaves the panel showing the last real frame.
  GpuVkState::PresentedImage presentedPicture() const;
  // A picture's own shape for the panel crop: its measured content rect where there is one.
  struct PaneSize {
    int width = 1, height = 1;
  };
  PaneSize pictureSize(const GpuVkState::PresentedImage &image) const;

  // ---- what a panel session is allowed to do
  // ------------------------------------------------------ Route this session's present into the
  // host's composite instead of the window, at `imageWidth` x `imageHeight`. See
  // gpu_vk_present_to_pane.
  void presentToPane(int imageWidth, int imageHeight);
  // Back to the ordinary product route: this session's present IS the window.
  void presentToWindow();
  // Whether the PLAYER's controller reaches this session's guest. A panel session is false — the
  // picker reads the pad itself, and a press that moved the selection must not also press a button
  // in three demos at once. The session the player confirms into is true, and that is where the
  // confirm press itself is delivered.
  void setPlayerInput(bool live);
  // Press these active-low buttons to the guest for the next `frames` pad frames. This is how the
  // confirm press REACHES the game it selected, as a real press rather than as a jump to a state.
  void pressOnce(std::uint16_t activeLowMask, int frames = 2);
  // Stop advancing / start advancing. A paused session keeps its picture and its guest state
  // exactly where they were; it does not present, and it does not hold the frame watchdog armed.
  void pause();
  void resume();
  bool paused() const {
    return paused_;
  }
  // This session alone may be heard. Exactly one session in a process holds it at a time.
  void setAudible(bool audible);
  // Take the process debug endpoint for this session. The endpoint outlives every Game and a claim
  // does not: the picker Game claimed it, the picker built its panels out of it, and confirming
  // destroys the picker Game — so the session that becomes the product has to claim it or every
  // later command times out on a healthy run. See psxport::DbgServer::claimEndpoint.
  void claimDebugEndpoint();

private:
  struct Parts;
  // Apply the present route to a session that has one. A panel is routed before its session has
  // booted (the host knows where every panel is long before that panel has a guest), so the route
  // is HELD as state and applied at boot instead of being a call that needs a machine to already
  // exist.
  void applyPresentRoute();

  const TitleAvailability &title_;
  bool selectorAvailable_;
  GpuDevice &presentation_;
  std::unique_ptr<Parts> parts_;
  End end_ = End::Running;
  bool paused_ = false;
  bool playerInput_ = false;
  bool audible_ = false;
  bool paneDestination_ = false;
  int paneImageW_ = 0, paneImageH_ = 0;
  // The last content answer, and the step it was taken at: a readback per frame would cost more
  // than the frames do, and the answer only changes when the guest draws something new. MUTABLE
  // because the answer is a CACHE of the session's own last frame, not state anyone else can
  // observe — asking "is your panel showing anything" must not need a non-const handle.
  mutable bool contentSeen_ = false;
  mutable std::uint64_t contentCheckedAtStep_ = 0;
  std::uint64_t steps_ = 0;
};

} // namespace spyro
