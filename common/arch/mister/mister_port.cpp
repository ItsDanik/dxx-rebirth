/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * MiSTer hybrid core support, see mister_port.h
 */

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <utility>
#include <SDL.h>
#include <mister_hybrid.h>
#include "mister_port.h"
#include "gr.h"
#include "console.h"

#ifdef DXX_MISTER_PROFILE
extern "C" void _mcleanup();
#endif

namespace dcx {

namespace {

constexpr int exit_to_menu{0};
/* danik_hybrid_launch.sh shows the list of games again */
constexpr int exit_to_list{42};
/* the same, after the core has settled: it was loaded again */
constexpr int exit_core_reloaded{43};

constexpr char title[]{"DXX-Rebirth"};

/* A field is 262 lines of 400 pixels at 6.25MHz (hybrid_host.sv) */
constexpr uint64_t field_pixels{400 * 262};
constexpr uint64_t pixel_clock{6250000};

/* Status bits of the OSD that mean the same in every hybrid core: 0 is
 * 100%, 10 is 0%
 */
constexpr int osd_sound_volume(const uint64_t status)
{
	return static_cast<int>((status >> 7) & 0xf);
}

constexpr int osd_music_volume(const uint64_t status)
{
	return static_cast<int>((status >> 11) & 0xf);
}

/* The game's options (CONF_STR in core/DXXRebirth.sv) */
constexpr bool osd_240_lines(const uint64_t status)
{
	return (status >> 24) & 1;
}

constexpr bool osd_no_rumble(const uint64_t status)
{
	return (status >> 25) & 1;
}

/* Bits 0..3 of MH_SetMenuMask(): "D0" greys the HUD margins in 320x200,
 * "D1" the cheats of only Descent II
 */
constexpr int menu_mask_200_lines{1};
constexpr int menu_mask_descent1{2};

/* GAME: 1 if the game's frames take most of a field: each is drawn a field
 * ahead and the library shows it at its own field (MH_PresentAtField()), so
 * that one a little slower than a field is still in time, for one field more
 * latency.  Decide by benchmark, hybrid/CLAUDE.md ("Frame pacing").
 */
#define DRAW_AHEAD 0

unsigned game_descent;
bool screen_240, menu_mask_sent;
void (*resolution_changed)();

void set_menu_mask()
{
	if (!MH_IsOpen())
		return;
	MH_SetMenuMask((screen_240 ? 0 : menu_mask_200_lines) | (game_descent == 1 ? menu_mask_descent1 : 0));
	menu_mask_sent = true;
}

void poll_resolution()
{
	if (!MH_IsOpen())
		return;
	if (!menu_mask_sent)
		set_menu_mask();
	if (osd_240_lines(MH_OSDStatus()) == screen_240)
		return;
	/* gr_set_mode() takes it from MiSTer_ScreenMode() */
	if (resolution_changed)
		(*resolution_changed)();
}

/* OSD triggers selected since the game last took them, while it does */
unsigned cheat_triggers;
bool cheat_triggers_taken;

void poll_triggers()
{
	if (!MH_IsOpen())
		return;
	const unsigned triggers = MH_OSDTriggers();
	/* those selected in a menu are not kept for the next game */
	cheat_triggers = (cheat_triggers_taken ? cheat_triggers : 0) | triggers;
	cheat_triggers_taken = false;
}

/* Rumble: a jolt for each motor, its strength fading out from `start` to
 * `end`; update_rumble() tells the controller every frame while one goes on
 */
struct jolt
{
	int level;
	uint64_t start, end;
};
std::array<jolt, 2> jolts;
bool rumbling;

uint64_t now_us()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<uint64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

int jolt_now(const jolt &j, const uint64_t now)
{
	if (now >= j.end)
		return 0;
	return static_cast<int>(j.level * (j.end - now) / (j.end - j.start));
}

void update_rumble()
{
	if (!MH_IsOpen())
		return;
	if (osd_no_rumble(MH_OSDStatus()))
		jolts = {};
	const auto now{now_us()};
	const int strong{jolt_now(jolts[0], now)}, weak{jolt_now(jolts[1], now)};
	/* once a frame while it goes on (the motors stop by themselves two
	 * seconds after the last call), and once more to stop them
	 */
	if (strong || weak || rumbling)
		MH_Rumble(strong, weak);
	rumbling = strong || weak;
}

void stop_rumble()
{
	if (rumbling)
		MH_Rumble(0, 0);
	rumbling = false;
}

void (*volume_changed)();

void poll_volume()
{
	static int last_sound, last_music;
	if (!MH_IsOpen() || !volume_changed)
		return;
	const auto status{MH_OSDStatus()};
	const int sound{osd_sound_volume(status)}, music{osd_music_volume(status)};
	if (sound == last_sound && music == last_music)
		return;
	last_sound = sound;
	last_music = music;
	(*volume_changed)();
}

uint32_t last_field;
uint64_t total_fields;
bool field_valid;
/* for the log: how well the game keeps up with the screen */
unsigned frame_count;

void check_core()
{
	/* Stays true if there never was a core */
	if (!MH_CheckAlive())
	{
		/* Loading the core again from the MiSTer menu is how the player
		 * gets back to the list of games (or starts this one afresh).
		 */
		const auto reloaded{MH_CoreReloaded()};
		con_printf(CON_NORMAL, "mister: %u frames in %u fields", frame_count, static_cast<unsigned>(total_fields));
		/* only a core that is still there stops the motors */
		jolts = {};
		rumbling = false;
		if (reloaded)
			con_puts(CON_NORMAL, "mister: the core was loaded again, leaving");
		else
			con_puts(CON_NORMAL, "mister: the core is gone, leaving");
		/* A profiling build (-pg -DDXX_MISTER_PROFILE) writes its gmon.out
		 * when the program ends in the usual way, which this is not.
		 */
#ifdef DXX_MISTER_PROFILE
		_mcleanup();
#endif
		std::_Exit(reloaded ? exit_core_reloaded : exit_to_menu);
	}
}


}

void MiSTer_Init(const unsigned descent)
{
	game_descent = descent;
	/* The game draws 8 bit: the core does the palette lookup, so palette
	 * fades and flashes cost nothing.
	 */
	SDL_SetHint("SDL_MISTER_VIDEO_FORMAT", "INDEX8");
	SDL_SetHint("SDL_MISTER_VSYNC", "1");
}

screen_mode MiSTer_ScreenMode()
{
	screen_240 = MH_IsOpen() && osd_240_lines(MH_OSDStatus());
	set_menu_mask();
	return screen_mode{320, screen_240 ? 240u : 200u};
}

void MiSTer_OnResolutionChange(void (*const changed)())
{
	resolution_changed = changed;
}

void MiSTer_HudMargins(int &top, int &bottom)
{
	MH_HudMargins(&top, &bottom);
}

grs_canvas *MiSTer_view_canvas;

bool MiSTer_RunTime(fix64 &runtime)
{
	/* The game has loops that wait for this clock, which stops with the
	 * core.
	 */
	check_core();
	if (!MH_IsOpen())
		return false;
	const uint32_t field{MH_FieldCounter()};
	if (!field_valid)
	{
		field_valid = true;
		last_field = field;
	}
	/* Never back: a core that was loaded again counts from 0 */
	if (const uint32_t passed{field - last_field}; passed < 0x80000000)
		total_fields += passed;
	last_field = field;
	runtime = static_cast<fix64>(total_fields * field_pixels * F1_0 / pixel_clock);
	return true;
}

bool MiSTer_WaitFrame()
{
	static uint32_t frame_field;
	static bool frame_field_valid;
	if (!MH_IsOpen())
		return false;
	if (!frame_field_valid)
	{
		frame_field_valid = true;
		frame_field = MH_FieldCounter();
	}
	if (DRAW_AHEAD && !MH_Interlaced())
	{
		/* the frame is shown at the field after the last one's */
		uint32_t target{frame_field + 1};
		/* not before the field two before its own */
		MH_WaitField(target - 3);
		const uint32_t field{MH_FieldCounter()};
		if (static_cast<int32_t>(field + 1 - target) > 0)
			/* too late for it: the next vblank */
			target = field + 1;
		MH_PresentAtField(target);
		frame_field = target;
		return true;
	}
	/* Returns at once if the field has passed, or the core is gone */
	MH_WaitField(frame_field);
	frame_field = MH_FieldCounter();
	return true;
}

bool MiSTer_WaitField()
{
	if (!MH_IsOpen() || !field_valid)
		return false;
	/* Returns at once if the core is gone */
	MH_WaitField(last_field);
	return MH_CheckAlive();
}

namespace {

/* MISTER_HYBRID_STATS=1 in the environment: every 10 seconds a line in the
 * log that tells how the frames met the fields of the screen.
 */
struct frame_stats
{
	bool enabled, started;
	unsigned frames;
	timespec flip_end;
	uint32_t present_field;
	/* fields from one frame on the screen to the next: 0, 1, 2, 3, more */
	std::array<unsigned, 5> fields;
	/* milliseconds from a flip to the next, without the wait in it:
	 * below 4, 8, 12, 14, 16, 20, 33, more
	 */
	std::array<unsigned, 8> work;
	unsigned max_work_us, total_work_us, max_flip_us, last_work_us, late_reports;
	/* in the frame that is being made */
	std::array<unsigned, 2> timed_us, timed_count;
	timespec flip_begin;
};

frame_stats stats;

unsigned elapsed_us(const timespec &from, const timespec &to)
{
	return static_cast<unsigned>((to.tv_sec - from.tv_sec) * 1000000 + (to.tv_nsec - from.tv_nsec) / 1000);
}

}

MiSTer_StatsTimer::MiSTer_StatsTimer(const unsigned w) :
	what{w}
{
	if (stats.enabled)
		clock_gettime(CLOCK_MONOTONIC, &start);
}

MiSTer_StatsTimer::~MiSTer_StatsTimer()
{
	if (!stats.enabled)
		return;
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	stats.timed_us[what] += elapsed_us(start, now);
	++stats.timed_count[what];
}

void MiSTer_FlipBegin()
{
	if (!stats.started)
	{
		stats.started = true;
		stats.enabled = getenv("MISTER_HYBRID_STATS") != nullptr;
	}
	if (!stats.enabled || !MH_IsOpen())
		return;
	clock_gettime(CLOCK_MONOTONIC, &stats.flip_begin);
	if (stats.flip_end.tv_sec)
	{
		constexpr std::array<unsigned, 7> limits{{4000, 8000, 12000, 14000, 16000, 20000, 33000}};
		const auto us{elapsed_us(stats.flip_end, stats.flip_begin)};
		std::size_t bucket{0};
		while (bucket < limits.size() && us >= limits[bucket])
			++bucket;
		++stats.work[bucket];
		stats.last_work_us = us;
		stats.max_work_us = std::max(stats.max_work_us, us);
		stats.total_work_us += us;
	}
}

void MiSTer_FlipEnd()
{
	if (!stats.enabled || !MH_IsOpen())
		return;
	const auto had_frame{stats.flip_end.tv_sec != 0};
	clock_gettime(CLOCK_MONOTONIC, &stats.flip_end);
	const auto flip_us{elapsed_us(stats.flip_begin, stats.flip_end)};
	stats.max_flip_us = std::max(stats.max_flip_us, flip_us);
	const uint32_t field{MH_FieldCounter()};
	/* A frame that was late, in a stretch where they are in time */
	if (had_frame && field - stats.present_field > 1 && stats.late_reports < 30)
	{
		++stats.late_reports;
		con_printf(CON_NORMAL, "mister: late frame %u, %u fields, work %u.%u ms (%u bitmaps read in %u.%u ms, %u log writes in %u.%u ms), flip %u.%u ms", stats.frames, field - stats.present_field, stats.last_work_us / 1000, stats.last_work_us % 1000 / 100,
			stats.timed_count[0], stats.timed_us[0] / 1000, stats.timed_us[0] % 1000 / 100, stats.timed_count[1], stats.timed_us[1] / 1000, stats.timed_us[1] % 1000 / 100, flip_us / 1000, flip_us % 1000 / 100);
	}
	stats.timed_us = {};
	stats.timed_count = {};
	if (had_frame)
		++stats.fields[std::min<uint32_t>(field - stats.present_field, stats.fields.size() - 1)];
	stats.present_field = field;
	if (++stats.frames < 600)
		return;
	con_printf(CON_NORMAL, "mister: 600 frames, fields per frame 0:%u 1:%u 2:%u 3:%u more:%u, work ms <4:%u <8:%u <12:%u <14:%u <16:%u <20:%u <33:%u more:%u, average %u.%u, longest %u.%u, longest flip %u.%u",
		stats.fields[0], stats.fields[1], stats.fields[2], stats.fields[3], stats.fields[4],
		stats.work[0], stats.work[1], stats.work[2], stats.work[3], stats.work[4], stats.work[5], stats.work[6], stats.work[7],
		stats.total_work_us / 600 / 1000, stats.total_work_us / 600 % 1000 / 100, stats.max_work_us / 1000, stats.max_work_us % 1000 / 100, stats.max_flip_us / 1000, stats.max_flip_us % 1000 / 100);
	const auto flip_end{stats.flip_end};
	const auto present_field{stats.present_field};
	stats = {};
	stats.enabled = stats.started = true;
	stats.flip_end = flip_end;
	stats.present_field = present_field;
}

void MiSTer_CheckCore()
{
	++frame_count;
	check_core();
	poll_volume();
	poll_resolution();
	poll_triggers();
	update_rumble();
}

namespace {

int scale_volume(const int volume, const int osd_volume)
{
	return volume * std::max(10 - osd_volume, 0) / 10;
}

}

int MiSTer_SoundVolume(const int volume)
{
	return scale_volume(volume, osd_sound_volume(MH_OSDStatus()));
}

int MiSTer_MusicVolume(const int volume)
{
	return scale_volume(volume, osd_music_volume(MH_OSDStatus()));
}

void MiSTer_OnVolumeChange(void (*const changed)())
{
	volume_changed = changed;
}

unsigned MiSTer_CheatOptions()
{
	return MH_IsOpen() ? static_cast<unsigned>((MH_OSDStatus() >> 26) & 0xff) : 0;
}

unsigned MiSTer_CheatTriggers()
{
	cheat_triggers_taken = true;
	return std::exchange(cheat_triggers, 0);
}

void MiSTer_Rumble(const int strong, const int weak, const int ms)
{
	if (!MH_IsOpen() || osd_no_rumble(MH_OSDStatus()) || ms <= 0)
		return;
	const auto now{now_us()};
	const std::array<int, 2> levels{{strong, weak}};
	for (std::size_t motor = 0; motor < jolts.size(); ++motor)
	{
		const int level{std::min(levels[motor], 255)};
		auto &j{jolts[motor]};
		if (level > jolt_now(j, now))
			j = {level, now, now + static_cast<uint64_t>(ms) * 1000};
	}
}

bool MiSTer_ShowError(const char *const message)
{
	if (!MH_Open())
		return false;
	MH_UI_Message(title, message, MH_UI_WAIT | MH_UI_ERROR);
	return true;
}

int MiSTer_ExitCode()
{
	stop_rumble();
	/* Not if we are quitting because another core was loaded */
	if (getenv("MISTER_HYBRID_FROM_LIST") && MH_Open())
		return exit_to_list;
	return exit_to_menu;
}

}
