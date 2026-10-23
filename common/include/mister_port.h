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

struct ADL_MIDIPlayer;

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

/* Before SDL_Init: pick the paletted screen, one frame per field.
 * `descent` is the game, 1 or 2 (for the OSD lines of only one of them).
 */
void MiSTer_Init(unsigned descent);
/* The size of the core's screen, which is the size of the window: 320x200,
 * or 320x240 as the OSD's Resolution says.  The game's screen is always
 * 320x200: in 320x240 it is the middle of the frame, and only the game's
 * view and HUD are drawn in all of it (gr.cpp, MiSTer_FrameCanvas()).
 */
screen_mode MiSTer_ScreenMode();
/* `changed` is called when the OSD's Resolution is changed */
void MiSTer_OnResolutionChange(void (*changed)());
/* gr.cpp: in 320x240 the whole frame, else nullptr */
grs_canvas *MiSTer_FrameCanvas();
/* gr.cpp: the pixels of the frame, where the texture mapper stops */
extern int MiSTer_frame_pixels;
/* gr.cpp: the game drew its view in all of this frame.  Else the rows
 * above and below the game's screen are made black when it is shown.
 */
void MiSTer_GameFrameDrawn();
/* The player's HUD margins in 320x240 (MH_HudMargins()): the rows from the
 * top of the frame to the top HUD, and from the bottom HUD to the bottom
 */
void MiSTer_HudMargins(int &top, int &bottom);
/* While the HUD is drawn to a canvas of its own (320x240): the canvas of
 * the view, for what is drawn where the view's objects are (the reticle,
 * the names of other players).  nullptr: they are the same canvas.
 */
extern grs_canvas *MiSTer_view_canvas;
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
/* Music played by the core (mister_music.cpp): libADLMIDI's sequencer
 * plays the song, and its OPL2's register writes go to the core's FM chip
 * or its MIDI events to the synthesizer in the core or an MT32-pi, as the
 * OSD's Music says.  Setup after adl_init(): false without the core's chip
 * (or the libADLMIDI of the MiSTer build), and the game plays it as on a PC.
 */
bool MiSTer_MusicSetup(ADL_MIDIPlayer *);
/* A song was opened */
void MiSTer_MusicSongStart();
/* In the audio callback, before libADLMIDI makes `frames` at `rate`:
 * follows the OSD, and returns the frames to leave silent before the song
 * goes on (while what was sent to an MT32-pi goes out)
 */
int MiSTer_MusicBlock(int rate, int frames);
/* The music stopped or paused, and goes on */
void MiSTer_MusicSilence();
void MiSTer_MusicResume();
/* The OSD's Cheats page: its options (bit 0: status bit 26, ...), and its
 * triggers (bit 0: T[56], ...) selected since the last call, of those
 * selected while the game called this every frame
 */
unsigned MiSTer_CheatOptions();
unsigned MiSTer_CheatTriggers();
/* Rumble of the controller (the OSD's Rumble): a jolt of the heavy and
 * the light motor, 0..255, fading out over `ms`.  A stronger jolt replaces
 * a weaker one.
 */
void MiSTer_Rumble(int strong, int weak, int ms);
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
