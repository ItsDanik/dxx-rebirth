/*
 * Portions of this file are copyright Rebirth contributors and licensed as
 * described in COPYING.txt.
 * Portions of this file are copyright Parallax Software and licensed
 * according to the Parallax license below.
 * See COPYING.txt for license details.

THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.
COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 *
 * Info on canvases, screens, etc.
 *
 */

#pragma once

#include "fwd-gr.h"

// What graphics modes the game & editor open

// for Screen_mode variable
#define SCREEN_MENU		0	// viewing the menu screen
#define SCREEN_GAME		1	// viewing the menu screen
#define SCREEN_EDITOR		2	// viewing the editor screen
#ifdef DXX_BUILD_DESCENT
#define SCREEN_MOVIE		3	// viewing a movie

//from game.c
namespace dsx {
int set_screen_mode(int sm);	// True = editor screen
}
#endif

namespace dcx {
extern const std::array<int8_t, 64> fades;

//About the screen
extern grs_subcanvas		Screen_3d_window;		// The rectangle for rendering the mine to

#if DXX_USE_MISTER_HYBRID
/* MiSTer in 320x240 (init_cockpit()): Screen_3d_window is the whole frame,
 * and this is where the view is in the 320x200 layout of the cockpit, which
 * the cockpit and HUD keep (game_render_frame_mono())
 */
struct mister_cockpit_layout
{
	bool active;
	uint16_t x, y, w, h;
};
extern mister_cockpit_layout MiSTer_cockpit_layout;
#endif
}
