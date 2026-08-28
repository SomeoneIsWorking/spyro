#include "fx_field_player_actor.h"

int main() {
  if (!spyro_field_player_visible(0u)) {
    return 1;
  }
  if (spyro_field_player_visible(1u)) {
    return 1;
  }
  if (spyro_field_player_visible(0xFFFFFFFFu)) {
    return 1;
  }
  return 0;
}
