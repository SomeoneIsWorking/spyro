#pragma once

class Core;

// Per-Game statement of which producer owns the picture that the next present must build. Boot
// starts with guest VRAM because Spyro's upload-only logos precede the title frame driver. Each
// explicit reference/native frame seam then replaces that default before it can present.
class SpyroPresentationOwner {
public:
  void beginGuestFrame() {
    owner_ = Owner::GuestVram;
  }

  void beginNativeFrame() {
    owner_ = Owner::NativeProducers;
  }

  bool guestVramIsPicture() const {
    return owner_ == Owner::GuestVram;
  }

private:
  enum class Owner { GuestVram, NativeProducers };
  Owner owner_ = Owner::GuestVram;
};

SpyroPresentationOwner &spyro_presentation_owner(Core &core);
const SpyroPresentationOwner &spyro_presentation_owner(const Core &core);
