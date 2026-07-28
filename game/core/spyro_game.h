// spyro_game.h — declarations for the Spyro game-side seam installers.
//
// The framework never includes this; it is purely how our own TUs (game_config.cpp, game_hooks.cpp,
// recomp_register.cpp, main.cpp) reach each other.
#pragma once

struct GameHooks;
class Game;

const GameHooks* spyro_game_hooks();     // game/core/game_hooks.cpp — the (mostly null) Phase-0 vtable
void spyro_install_game_config();        // game/core/game_config.cpp — installs GameConfig + hooks
void spyro_install_recomp();             // game/core/recomp_register.cpp — installs the generated substrate

// game/core/vsync.cpp — installs the vblank timebase (the libetc wait helper).
void spyro_register_vsync(Game* g);
