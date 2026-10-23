/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 *
 * SDL video functions.
 *
 */

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <SDL.h>
#include "gr.h"
#include "grdef.h"
#include "palette.h"
#include "game.h"
#include "console.h"
#include "u_mem.h"
#include "dxxerror.h"
#include "vers_id.h"
#include "gamefont.h"
#include "args.h"
#include "config.h"
#include "palette.h"
#include "mister_port.h"

#include "compiler-range_for.h"
#include "d_range.h"
#include <memory>

using std::min;

namespace dcx {

#if SDL_MAJOR_VERSION == 1
static int sdl_video_flags = SDL_SWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF;
#elif SDL_MAJOR_VERSION == 2
SDL_Window *g_pRebirthSDLMainWindow;
#endif
static SDL_Surface *screen, *canvas;
static int gr_installed;

#if SDL_MAJOR_VERSION == 1
void gr_flip()
{
	SDL_BlitSurface(canvas, nullptr, screen, nullptr);
	SDL_Flip(screen);
}

static void gr_set_canvas_colors(const std::array<SDL_Color, 256> &colors)
{
	SDL_SetColors(canvas, const_cast<SDL_Color *>(colors.data()), 0, colors.size());
}
#elif SDL_MAJOR_VERSION == 2
void gr_flip()
{
	const auto SDLWindow{g_pRebirthSDLMainWindow};
	if (!canvas)
		return;
	/* The window surface is replaced when the window changes size.  A
	 * canvas that is the window surface only changes size in
	 * gr_set_mode.
	 */
	if (canvas != screen)
	{
		screen = SDL_GetWindowSurface(SDLWindow);
		if (!screen)
			return;
		if (screen->w == canvas->w && screen->h == canvas->h)
			SDL_BlitSurface(canvas, nullptr, screen, nullptr);
		else
			SDL_BlitScaled(canvas, nullptr, screen, nullptr);
	}
#if DXX_USE_MISTER_HYBRID
	MiSTer_FlipBegin();
	SDL_UpdateWindowSurface(SDLWindow);
	MiSTer_FlipEnd();
#else
	SDL_UpdateWindowSurface(SDLWindow);
#endif
}

static void gr_set_canvas_colors(const std::array<SDL_Color, 256> &colors)
{
	SDL_SetPaletteColors(canvas->format->palette, colors.data(), 0, colors.size());
}
#endif

#if SDL_MAJOR_VERSION == 1
// returns possible (fullscreen) resolutions if any.
uint_fast32_t gr_list_modes(std::array<screen_mode, 50> &gsmodes)
{
	int modesnum{0};
	int sdl_check_flags = sdl_video_flags;

	sdl_check_flags |= SDL_FULLSCREEN; // always use Fullscreen as lead.

	SDL_Rect **const modes{SDL_ListModes(NULL, sdl_check_flags)};
	if (modes == nullptr) // check if we get any modes - if not, return 0
		return 0;

	if (modes == reinterpret_cast<SDL_Rect**>(-1))
	{
		return 0; // can obviously use any resolution... strange!
	}
	else
	{
		for (int i = 0; modes[i]; ++i)
		{
			if (modes[i]->w > 0xFFF0 || modes[i]->h > 0xFFF0 // resolutions saved in 32bits. so skip bigger ones (unrealistic in 2010) (kreatordxx - made 0xFFF0 to kill warning)
				|| modes[i]->w < 320 || modes[i]->h < 200) // also skip everything smaller than 320x200
				continue;
			gsmodes[modesnum].width = modes[i]->w;
			gsmodes[modesnum].height = modes[i]->h;
			modesnum++;
			if (modesnum >= gsmodes.size()) // that really seems to be enough big boy.
				break;
		}
		return modesnum;
	}
}
#endif

}

namespace dsx {

#if SDL_MAJOR_VERSION == 2
void gr_set_mode_from_window_size()
{
	gr_set_mode(Game_screen_mode);
}

int gr_set_mode(screen_mode mode)
{
	const auto SDLWindow{g_pRebirthSDLMainWindow};
#if DXX_USE_MISTER_HYBRID
	/* The window is the screen of the core. */
	mode = MiSTer_ScreenMode();
	Game_screen_mode = mode;
#endif
	const unsigned w = SM_W(mode), h = SM_H(mode);
	if (!(SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP))
		SDL_SetWindowSize(SDLWindow, w, h);
	screen = SDL_GetWindowSurface(SDLWindow);
	if (!screen)
	{
		Error("Could not get the window surface: %s\n", SDL_GetError());
		exit(1);
	}

	/* Release the previous canvas, which the screen owns unless it was
	 * the window surface.
	 */
	canvas = nullptr;
	*grd_curscreen = {};
	if (screen->format->BitsPerPixel == 8 && screen->format->palette && static_cast<unsigned>(screen->w) == w && static_cast<unsigned>(screen->h) == h && !SDL_MUSTLOCK(screen))
		/* A paletted window surface: draw to it directly. */
		canvas = screen;
	else
	{
		canvas = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
		if (canvas == NULL)
		{
			Error("Could not create canvas surface\n");
			exit(1);
		}
		grd_curscreen->sdl_surface = RAII_SDL_Surface(canvas);
	}
	grd_curscreen->set_screen_width_height(w, h);
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->get_screen_width() * CGameCfg.AspectX, grd_curscreen->get_screen_height() * CGameCfg.AspectY);
	gr_init_canvas(grd_curscreen->sc_canvas, reinterpret_cast<unsigned char *>(canvas->pixels), bm_mode::linear, w, h);
	window_update_canvases();
	gr_set_default_canvas();

	SDL_ShowCursor(0);
	gamefont_choose_game_font(w,h);
	gr_palette_load(gr_palette);
	gr_remap_color_fonts();

	return 0;
}
#elif SDL_MAJOR_VERSION == 1
void gr_set_mode_from_window_size()
{
	gr_set_mode(Game_screen_mode);
}

int gr_set_mode(screen_mode mode)
{
	screen=NULL;

	SDL_WM_SetCaption(DESCENT_VERSION, DXX_SDL_WINDOW_CAPTION);
	SDL_WM_SetIcon( SDL_LoadBMP( DXX_SDL_WINDOW_ICON_BITMAP ), NULL );

	const auto sdl_video_flags = ::sdl_video_flags;
	const auto DbgBpp{CGameArg.DbgBpp};
	if(SDL_VideoModeOK(SM_W(mode), SM_H(mode), DbgBpp, sdl_video_flags))
	{
	}
	else
	{
		con_printf(CON_URGENT,"Cannot set %hux%hu. Fallback to 640x480", SM_W(mode), SM_H(mode));
		mode.width = 640;
		mode.height = 480;
		Game_screen_mode = mode;
	}
	const unsigned w = SM_W(mode), h = SM_H(mode);
	screen = SDL_SetVideoMode(w, h, DbgBpp, sdl_video_flags);

	if (screen == NULL)
	{
		Error("Could not set %dx%dx%d video mode\n", w, h, DbgBpp);
		exit(1);
	}

	canvas = SDL_CreateRGBSurface(sdl_video_flags, w, h, 8, 0, 0, 0, 0);
	if (canvas == NULL)
	{
		Error("Could not create canvas surface\n");
		exit(1);
	}

	*grd_curscreen = {};
	grd_curscreen->sdl_surface = RAII_SDL_Surface(canvas);
	grd_curscreen->set_screen_width_height(w, h);
	grd_curscreen->sc_aspect = fixdiv(grd_curscreen->get_screen_width() * CGameCfg.AspectX, grd_curscreen->get_screen_height() * CGameCfg.AspectY);
	gr_init_canvas(grd_curscreen->sc_canvas, reinterpret_cast<unsigned char *>(canvas->pixels), bm_mode::linear, w, h);
	window_update_canvases();
	gr_set_default_canvas();

	SDL_ShowCursor(0);
	gamefont_choose_game_font(w,h);
	gr_palette_load(gr_palette);
	gr_remap_color_fonts();

	return 0;
}
#endif

}

namespace dcx {

#if SDL_MAJOR_VERSION == 1
int gr_check_fullscreen(void)
{
	return !!(sdl_video_flags & SDL_FULLSCREEN);
}

void gr_toggle_fullscreen()
{
	sdl_video_flags ^= SDL_FULLSCREEN;
	const int WindowMode = !(sdl_video_flags & SDL_FULLSCREEN);
	CGameCfg.WindowMode = WindowMode;
	gr_remap_color_fonts();
	SDL_WM_ToggleFullScreen(screen);
}
#elif SDL_MAJOR_VERSION == 2
int gr_check_fullscreen(void)
{
	return !!(SDL_GetWindowFlags(g_pRebirthSDLMainWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void gr_toggle_fullscreen()
{
#if !DXX_USE_MISTER_HYBRID
	/* The canvas keeps its size; gr_flip scales it to the window. */
	const auto SDLWindow{g_pRebirthSDLMainWindow};
	const auto is_fullscreen_before_change{SDL_GetWindowFlags(SDLWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP};
	CGameCfg.WindowMode = !!is_fullscreen_before_change;
	SDL_SetWindowFullscreen(SDLWindow, is_fullscreen_before_change ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}
#endif

}

namespace dsx {

int gr_init()
{
	// Only do this function once!
	if (gr_installed==1)
		return -1;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		Error("SDL library video initialisation failed: %s.",SDL_GetError());
	}

	grd_curscreen = std::make_unique<grs_screen>();

#if SDL_MAJOR_VERSION == 1
	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_video_flags|=SDL_FULLSCREEN;

	if (CGameArg.SysNoBorders)
		sdl_video_flags|=SDL_NOFRAME;

	if (CGameArg.DbgSdlHWSurface)
		sdl_video_flags|=SDL_HWSURFACE;

	if (CGameArg.DbgSdlASyncBlit)
		sdl_video_flags|=SDL_ASYNCBLIT;
#elif SDL_MAJOR_VERSION == 2
	assert(!g_pRebirthSDLMainWindow);
	unsigned sdl_window_flags{0};
	if (CGameArg.SysNoBorders)
		sdl_window_flags |= SDL_WINDOW_BORDERLESS;
#if DXX_USE_MISTER_HYBRID
	const auto mode{MiSTer_ScreenMode()};
#else
	if (!CGameCfg.WindowMode && !CGameArg.SysWindow)
		sdl_window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	const auto mode{Game_screen_mode};
#endif
	const auto SDLWindow{SDL_CreateWindow(DESCENT_VERSION, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SM_W(mode), SM_H(mode), sdl_window_flags)};
	if (!SDLWindow)
		return -1;
	g_pRebirthSDLMainWindow = SDLWindow;
	if (const auto window_icon{SDL_LoadBMP(DXX_SDL_WINDOW_ICON_BITMAP)})
	{
		SDL_SetWindowIcon(SDLWindow, window_icon);
		SDL_FreeSurface(window_icon);
	}
#endif

	// Set the mode.
	grd_curscreen->sc_canvas.cv_fade_level = GR_FADE_OFF;
	grd_curscreen->sc_canvas.cv_font = NULL;
	grd_curscreen->sc_canvas.cv_font_fg_color = 0;
	grd_curscreen->sc_canvas.cv_font_bg_color = 0;
	gr_set_current_canvas( &grd_curscreen->sc_canvas );

	gr_installed = 1;

	return 0;
}

void gr_close()
{
	if (gr_installed==1)
	{
		gr_installed = 0;
		grd_curscreen.reset();
		canvas = nullptr;
#if SDL_MAJOR_VERSION == 2
		screen = nullptr;
		if (const auto SDLWindow{std::exchange(g_pRebirthSDLMainWindow, nullptr)})
			SDL_DestroyWindow(SDLWindow);
#endif
		SDL_ShowCursor(1);
	}
}

}

namespace dcx {

// Palette functions follow.
static int last_r=0, last_g=0, last_b=0;

void gr_palette_step_up( int r, int g, int b )
{
	palette_array_t &p = gr_palette;
	SDL_Palette *palette;

	if ( (r==last_r) && (g==last_g) && (b==last_b) )
		return;

	last_r = r;
	last_g = g;
	last_b = b;

	if (canvas == NULL)
		return;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	std::array<SDL_Color, 256> colors{};
	range_for (const int i, xrange(256u))
	{
		const auto ir = static_cast<int>(p[i].r) + r + gr_palette_gamma;
		colors[i].r = std::clamp(ir, 0, 63) * 4;
		const auto ig = static_cast<int>(p[i].g) + g + gr_palette_gamma;
		colors[i].g = std::clamp(ig, 0, 63) * 4;
		const auto ib = static_cast<int>(p[i].b) + b + gr_palette_gamma;
		colors[i].b = std::clamp(ib, 0, 63) * 4;
	}
	gr_set_canvas_colors(colors);
}

void gr_palette_load(const palette_array_t &pal)
{
	SDL_Palette *palette;
	std::array<uint8_t, 64> gamma;

	if (canvas && pal != gr_current_pal)
		SDL_FillRect(canvas, NULL, SDL_MapRGB(canvas->format, 0, 0, 0));

	copy_bound_palette(gr_current_pal, pal);

	if (canvas == NULL)
		return;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	range_for (const int i, xrange(64u))
		gamma[i] = static_cast<int>((pow((14.0 / 32.0), 1.0) * i) + 0.5);

	/* This row of the fade table takes 256 searches for the closest
	 * colour, and the menus load the palette for every frame they draw,
	 * nearly always the same one.  The row is left alone if it is still
	 * what was made here from the same palette.
	 */
	static palette_array_t fade_row_palette;
	static std::array<color_palette_index, 256> fade_row;
	static bool fade_row_valid;
	auto &current_fade_row{gr_fade_table[(gr_fade_level{14})]};
	const bool fade_row_current{fade_row_valid && fade_row_palette == gr_palette && std::equal(fade_row.begin(), fade_row.end(), current_fade_row.begin())};

	std::array<SDL_Color, 256> colors{};
	for (int i = 0, j = 0; j < 256; j++)
	{
		if (!fade_row_current)
			current_fade_row[j] = fade_row[j] = gr_find_closest_color(gamma[gr_palette[j].r], gamma[gr_palette[j].g], gamma[gr_palette[j].b]);
		colors[j].r = (min(gr_current_pal[i].r + gr_palette_gamma, 63)) * 4;
		colors[j].g = (min(gr_current_pal[i].g + gr_palette_gamma, 63)) * 4;
		colors[j].b = (min(gr_current_pal[i].b + gr_palette_gamma, 63)) * 4;
		i++;
	}

	fade_row_palette = gr_palette;
	fade_row_valid = true;
	gr_set_canvas_colors(colors);
	reset_computed_colors();
	gr_remap_color_fonts();
}

void gr_palette_read(palette_array_t &pal)
{
	SDL_Palette *palette;
	unsigned i;

	palette = canvas->format->palette;

	if (palette == NULL)
		return; // Display is not palettised

	for (i = 0; i < 256; i++)
	{
		pal[i].r = palette->colors[i].r / 4;
		pal[i].g = palette->colors[i].g / 4;
		pal[i].b = palette->colors[i].b / 4;
	}
}

}
