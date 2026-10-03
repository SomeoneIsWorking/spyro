#pragma once

class Core;

namespace spyro {

// Per-Game statement of which producer owns the picture that the next present must build. Boot
// starts with guest VRAM because Spyro's upload-only logos precede the title frame driver. Each
// explicit reference/native frame seam then replaces that default before it can present.
class PresentationOwner {
public:
  void beginGuestFrame() {
    owner_ = Owner::GuestReference;
  }

  void beginNativeFrame() {
    owner_ = Owner::NativeProducers;
  }

  bool guestVramIsPicture() const {
    return owner_ != Owner::NativeProducers;
  }

  // The boot logos are guest uploads at the native width; no widening applies to them.
  bool guestPictureIsNativeWidth() const {
    return owner_ == Owner::BootUploads;
  }

private:
  // BootUploads: the upload-only logos before the frame driver; GuestReference: the diagnostic
  // guest-render leg; NativeProducers: the shipping native frame.
  enum class Owner { BootUploads, GuestReference, NativeProducers };
  Owner owner_ = Owner::BootUploads;
};

// The process-lifetime owner of the selected title, published with the context it belongs to.
PresentationOwner &presentationOwner(Core &core);
const PresentationOwner &presentationOwner(const Core &core);

} // namespace spyro
