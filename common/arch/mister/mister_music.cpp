/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * MiSTer hybrid core: the music is played by the core, see mister_port.h
 */

#include "dxxsconf.h"

#if DXX_USE_ADLMIDI
#include <array>
#include <atomic>
#include <cstring>
#include <SDL.h>
#include <SDL_mixer.h>
#include <mister_hybrid.h>
#include "adlmidi_dynamic.h"
#include "console.h"
#include "mister_port.h"

namespace dcx {

namespace {

/* MH_FMVolume() and MH_MidiVolume() at the game's full music volume.  The
 * software chip is played twice as loud as libADLMIDI makes it
 * (mix_adlmidi()), the core's chip at 32 is as loud as that comes.
 */
constexpr int fm_full_volume{64};
constexpr int midi_full_volume{32};

/* the core plays the music: libADLMIDI runs ADLMIDI_EMU_HOOK_OPL2 */
bool core_plays;
/* where it goes now: MIDI (the synthesizer or an MT32-pi) or the FM chip */
bool midi_on;
/* a switch to MIDI is counted by the library as a change of synthesizer
 * too, at the end of the next block: not to be answered twice
 */
int midi_switching;
std::atomic<bool> song_started;
int last_fm_volume{-1}, last_midi_volume{-1};

/* The OPL2's registers as libADLMIDI wrote them, to load the chip again */
std::array<uint8_t, 0x100> fm_shadow;

/* What the song has set on each MIDI channel, to tell a synthesizer that
 * comes in the middle of it (or has forgotten)
 */
struct midi_channel
{
	int program{-1};
	int bend_lsb{-1}, bend_msb{-1};
	std::array<int8_t, 128> controller;
	midi_channel()
	{
		controller.fill(-1);
	}
};
std::array<midi_channel, 16> midi_state;

/* The block the audio callback fills: its first frame in adl_chipFrame()'s
 * count, the mixer's rate, and the callback's thread
 */
unsigned long block_frame;
int block_rate{MH_AUDIO_RATE};
SDL_threadID audio_thread;

/* `ahead` of MH_FMWrite() and MH_MidiEvent(): the frame of the block, in
 * the core's 44.1kHz; 0 ("now") outside the audio callback
 */
int ahead_of(const unsigned long frame)
{
	if (SDL_ThreadID() != audio_thread)
		return 0;
	return static_cast<int>(uint64_t{frame - block_frame} * MH_AUDIO_RATE / block_rate);
}

void fm_write(void *, const int reg, const int value, const unsigned long frame)
{
	fm_shadow[reg] = value;
	if (!midi_on)
		MH_FMWrite(reg, value, ahead_of(frame));
}

/* The chip as the music has left it: enabling clears it, then the
 * registers, the notes playing (0xB0..0xB8) last
 */
void fm_load(const int ahead)
{
	MH_FMEnable(1);
	MH_FMWrite(0x01, fm_shadow[0x01], ahead);
	MH_FMWrite(0x08, fm_shadow[0x08], ahead);
	for (int reg = 0x20; reg < 0x100; ++reg)
		if (reg < 0xb0 || reg > 0xb8)
			MH_FMWrite(reg, fm_shadow[reg], ahead);
	for (int reg = 0xb0; reg <= 0xb8; ++reg)
		MH_FMWrite(reg, fm_shadow[reg], ahead);
}

/* Key off on all nine channels, the registers kept */
void fm_silence(const int ahead)
{
	for (int reg = 0xb0; reg <= 0xb8; ++reg)
		MH_FMWrite(reg, fm_shadow[reg] & ~0x20, ahead);
}

/* A General MIDI reset (the synthesizer that ran before may have set
 * anything), then what the song has set on each channel
 */
void midi_send_state(const int ahead)
{
	static const uint8_t gm_on[]{0x7e, 0x7f, 0x09, 0x01};
	MH_MidiSysEx(ahead, gm_on, sizeof(gm_on));
	for (int c = 0; c < 16; ++c)
	{
		const auto &s{midi_state[c]};
		for (int n = 0; n < 120; ++n)
			if (s.controller[n] >= 0)
				MH_MidiEvent(ahead, 0xb0 | c, n, s.controller[n]);
		if (s.program >= 0)
			MH_MidiEvent(ahead, 0xc0 | c, s.program, 0);
		if (s.bend_msb >= 0)
			MH_MidiEvent(ahead, 0xe0 | c, s.bend_lsb, s.bend_msb);
	}
}

void midi_notes_off(const int ahead)
{
	for (int c = 0; c < 16; ++c)
	{
		/* All Sound Off, All Notes Off */
		MH_MidiEvent(ahead, 0xb0 | c, 120, 0);
		MH_MidiEvent(ahead, 0xb0 | c, 123, 0);
	}
}

void midi_event(void *, const uint8_t type, uint8_t, const uint8_t channel, const uint8_t *const data, const std::size_t len)
{
	const auto ahead{ahead_of(adl_chipFrame())};
	if (type >= 0x8 && type <= 0xe && channel < 16)
	{
		const int d1{len > 0 ? data[0] & 0x7f : 0}, d2{len > 1 ? data[1] & 0x7f : 0};
		auto &s{midi_state[channel]};
		switch (type)
		{
			case 0xb:
				/* the channel mode messages are no setting of the song's */
				if (d1 < 120)
					s.controller[d1] = d2;
				else if (d1 == 121)
					s.controller.fill(-1);
				break;
			case 0xc:
				s.program = d1;
				break;
			case 0xe:
				s.bend_lsb = d1;
				s.bend_msb = d2;
				break;
		}
		if (midi_on)
			MH_MidiEvent(ahead, (type << 4) | channel, d1, d2);
	}
	else if ((type == 0xf0 || type == 0xf7) && midi_on && len > 0)
	{
		/* without its F0 and F7 */
		std::size_t first{data[0] == 0xf0 ? 1u : 0u}, end{len};
		if (end > first && data[end - 1] == 0xf7)
			--end;
		if (end > first)
			MH_MidiSysEx(ahead, data + first, static_cast<int>(end - first));
	}
}

void set_volumes()
{
	const int volume{Mix_VolumeMusic(-1)};
	const int fm{fm_full_volume * volume / MIX_MAX_VOLUME}, midi{midi_full_volume * volume / MIX_MAX_VOLUME};
	if (fm != last_fm_volume)
		MH_FMVolume(last_fm_volume = fm);
	if (midi != last_midi_volume)
		MH_MidiVolume(last_midi_volume = midi);
}

}

bool MiSTer_MusicSetup(ADL_MIDIPlayer *const adlmidi)
{
	core_plays = adl_setChipWriteHook && MH_IsOpen() && MH_FMAvailable() &&
		adl_switchEmulator(adlmidi, ADLMIDI_EMU_HOOK_OPL2) == 0;
	if (!core_plays)
	{
		con_puts(CON_NORMAL, "mister: the music is played by a software OPL3");
		return false;
	}
	adl_setNumChips(adlmidi, 1);
	adl_setChipWriteHook(&fm_write, nullptr);
	if (adl_setRawEventHook)
		adl_setRawEventHook(adlmidi, &midi_event, nullptr);
	MH_FMEnable(1);
	MH_MidiEnable(1);
	midi_on = false;
	con_puts(CON_NORMAL, "mister: the music is played by the core");
	return true;
}

void MiSTer_MusicSongStart()
{
	if (core_plays)
		song_started = true;
}

int MiSTer_MusicBlock(const int rate, const int frames)
{
	if (!core_plays)
		return 0;
	audio_thread = SDL_ThreadID();
	block_rate = rate;
	block_frame = adl_chipFrame();
	set_volumes();
	int hold{0};
	const bool new_song{song_started.exchange(false)};
	if (new_song)
		for (auto &s : midi_state)
			s = {};
	const bool wanted{MH_MidiWanted() != 0};
	/* our own switch to MIDI is not answered again */
	const bool changed{midi_on && MH_MidiSynthChanged() && !midi_switching};
	if (midi_switching)
		--midi_switching;
	if (wanted != midi_on)
	{
		midi_on = wanted;
		if (wanted)
		{
			fm_silence(0);
			midi_send_state(0);
			midi_switching = 2;
		}
		else
		{
			MH_MidiReset(0);
			fm_load(0);
		}
		hold = MH_MidiBacklog();
	}
	else if (midi_on && (changed || new_song))
	{
		/* the synthesizer lost the song's settings, or a new song starts
		 * on what the last one set
		 */
		midi_send_state(0);
		hold = MH_MidiBacklog();
	}
	/* While what was just sent goes out to an MT32-pi, the song waits: its
	 * first notes would go out late, their ends nearly on time
	 */
	hold = hold * rate / MH_AUDIO_RATE;
	if (hold > frames)
		hold = frames;
	/* the frames held are silence, the song starts after them */
	block_frame -= hold;
	return hold;
}

void MiSTer_MusicSilence()
{
	if (!core_plays)
		return;
	if (midi_on)
		midi_notes_off(0);
	else
		fm_silence(0);
}

void MiSTer_MusicResume()
{
	if (core_plays && !midi_on)
		fm_load(0);
}

}
#endif
