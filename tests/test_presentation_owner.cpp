#include "presentation_owner.h"

int main() {
  SpyroPresentationOwner owner;
  if (!owner.guestVramIsPicture()) {
    return 1;
  }
  owner.beginNativeFrame();
  if (owner.guestVramIsPicture()) {
    return 2;
  }
  owner.beginGuestFrame();
  return owner.guestVramIsPicture() ? 0 : 3;
}
