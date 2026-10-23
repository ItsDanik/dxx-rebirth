/*
 * This file is part of the DXX-Rebirth project <https://github.com/dxx-rebirth/dxx-rebirth/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This is a dynamic interface to libADLMIDI, the OPL3 synthesizer library.
 */

#include <cstddef>
#include <memory>
#include <stdint.h>
#include "dxxsconf.h"

#if DXX_USE_ADLMIDI
struct ADL_MIDIPlayer;

enum ADLMIDI_SampleType
{
	ADLMIDI_SampleType_S16 = 0
};

struct ADLMIDI_AudioFormat
{
	enum ADLMIDI_SampleType type;
	unsigned containerSize;
	unsigned sampleOffset;
};

enum ADL_Emulator
{
    ADLMIDI_EMU_DOSBOX = 2,
#if DXX_USE_MISTER_HYBRID
    /* The libADLMIDI of the MiSTer build (tools/libadlmidi-mister.patch of
     * DXXRebirth_MiSTer): an OPL2 whose register writes go to a hook, for
     * the FM chip of the core
     */
    ADLMIDI_EMU_HOOK_OPL2 = 14,
#endif
};

/*
 * A few embedded bank numbers.
 */
enum class ADL_EmbeddedBank
{
	MILES_AIL = 0,
	BISQWIT = 1,
	DESCENT = 2,
	// DESCENT_INT = 3,
	// DESCENT_HAM = 4,
	// DESCENT_RICK = 5,
	// DESCENT2 = 6,
	LBA_4OP = 31,
	THE_FATMAN_2OP = 58,
	THE_FATMAN_4OP = 59,
	JAMIE_OCONNELL = 66,
	NGUYEN_WOHLSTAND_4OP = 68,
	DMXOPL3 = 72,
	APOGEE_IMF90 = 74,
};

extern ADL_MIDIPlayer *(*adl_init)(long sample_rate);
extern void (*adl_close)(ADL_MIDIPlayer *device);
extern int (*adl_switchEmulator)(ADL_MIDIPlayer *device, int emulator);
extern int (*adl_setNumChips)(ADL_MIDIPlayer *device, int numChips);
extern int (*adl_setBank)(ADL_MIDIPlayer *device, int bank);
extern void (*adl_setSoftPanEnabled)(ADL_MIDIPlayer *device, int softPanEn);
extern void (*adl_setLoopEnabled)(ADL_MIDIPlayer *device, int loopEn);
extern int (*adl_openData)(ADL_MIDIPlayer *device, const void *mem, unsigned long size);
extern int (*adl_openFile)(ADL_MIDIPlayer *device, const char *filePath);
extern int (*adl_playFormat)(ADL_MIDIPlayer *device, int sampleCount, uint8_t *left, uint8_t *right, const ADLMIDI_AudioFormat *format);
#if DXX_USE_MISTER_HYBRID
/* Each MIDI event of the song as it is played */
using ADL_RawEventHook = void (*)(void *userdata, uint8_t type, uint8_t subtype, uint8_t channel, const uint8_t *data, std::size_t len);
extern void (*adl_setRawEventHook)(ADL_MIDIPlayer *device, ADL_RawEventHook rawEventHook, void *userData);
/* ADLMIDI_EMU_HOOK_OPL2's register writes, with the output frame each is
 * made at (adl_chipFrame(): frames made so far).  Both are nullptr with a
 * libADLMIDI that does not have them.
 */
using ADL_ChipWriteHook = void (*)(void *userdata, int reg, int value, unsigned long frame);
extern void (*adl_setChipWriteHook)(ADL_ChipWriteHook chipWriteHook, void *userData);
extern unsigned long (*adl_chipFrame)();
#endif

struct ADLMIDI_delete
{
	static void operator()(ADL_MIDIPlayer *x)
	{
		adl_close(x);
	}
};

typedef std::unique_ptr<ADL_MIDIPlayer, ADLMIDI_delete> ADL_MIDIPlayer_t;
#endif
