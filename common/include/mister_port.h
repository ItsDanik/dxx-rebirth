/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * MiSTer hybrid core: the game runs on the ARM of the MiSTer, the FPGA
 * core shows the picture at 15kHz and provides audio and input.  SDL's
 * "mister" drivers (Hybrid_MiSTer) do most of the work; this is what is
 * specific to the game.  Everything is a no-op without the core, so a
 * PC build with mister_hybrid=1 still runs.
 */

#pragma once

#include "dxxsconf.h"

#if DXX_USE_MISTER_HYBRID
#include "fwd-gr.h"
#include "maths.h"
#include <ctime>

namespace dcx {

/* Joystick buttons as SDL sees them: the buttons of the core's "J1,"
 * list in order, then the two the SDL driver adds for menus.
 */
enum mister_joy_button : unsigned
{
	/* opens the game menu and the automap from it, and closes them */
	menu = 7,
	/* spare buttons of the core, for a pad with more than eight */
	core_menu_ok = 8,
	core_menu_back = 9,
	/* the buttons the OSD options Menu OK and Menu Back name */
	menu_ok = 28,
	menu_back = 29,
};

/* Before SDL_Init: pick the paletted screen, one frame per field. */
void MiSTer_Init();
/* The size of the core's screen, which is the size of the window. */
screen_mode MiSTer_ScreenMode();
/* The game's clock, counted in fields of the core (1/59.64s) so that a
 * frame is on the screen for exactly as long as the game thinks it is.
 * Returns false without the core: use the system clock then.
 */
bool MiSTer_RunTime(fix64 &runtime);
/* At the start of a frame of the game: sleep until the field after the one
 * the frame before started in.  The frame is then made and shown at once,
 * which is a field sooner after the player's input than waiting when it
 * is shown.  Returns false without the core.
 */
bool MiSTer_WaitFrame();
/* Sleep until the clock of MiSTer_RunTime has advanced.  Returns false
 * without the core.
 */
bool MiSTer_WaitField();
/* Call once per frame.  Looks at the OSD, and does not return once another
 * core was loaded:
 * the game may be anywhere in its nested menus then, and has to be gone
 * before the launcher of the other core starts.  Settings, pilots and
 * saved games are written when they change, so nothing is lost.
 */
void MiSTer_CheckCore();
/* Around the call that shows a frame, for the statistics that
 * MISTER_HYBRID_STATS=1 in the environment puts in the log.
 */
void MiSTer_FlipBegin();
void MiSTer_FlipEnd();
/* For those statistics: the time something takes that a frame may have to
 * wait for, from here to the end of the scope.  0 is reading a bitmap from
 * the game data, 1 is writing to the log.
 */
struct MiSTer_StatsTimer
{
	const unsigned what;
	timespec start;
	MiSTer_StatsTimer(unsigned what);
	~MiSTer_StatsTimer();
	MiSTer_StatsTimer(const MiSTer_StatsTimer &) = delete;
};
/* Sound Volume and Music Volume of the OSD, on top of the volumes set in
 * the game.  Both take a volume and return what is left of it.
 */
int MiSTer_SoundVolume(int volume);
int MiSTer_MusicVolume(int volume);
/* `changed` is called when one of the two is changed in the OSD, for the
 * game to set its volumes again.
 */
void MiSTer_OnVolumeChange(void (*changed)());
/* Returns true if the message was shown on the screen of the core, where
 * it stays until a button is pressed.
 */
bool MiSTer_ShowError(const char *message);
/* Exit code of the process after the player quit: the launcher shows the
 * list of games again or returns to the MiSTer menu.
 */
int MiSTer_ExitCode();

}
#endif
