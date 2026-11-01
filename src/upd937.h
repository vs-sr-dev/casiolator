/* Casiolator - the Loopy's sound chip: NEC CDT109 (a cost-reduced uPD937) with its
 * 512 KB sample ROM, driven by MIDI. Clean room: what we know is in docs/loopy-sound.md. */
#ifndef CL_UPD937_H
#define CL_UPD937_H

#include <stdint.h>

#define UPD937_RATE (21725000.0 / 256)  /* output frames a second (84863 Hz) */
#define UPD937_ROM  0x80000
#define UPD937_GENS 32                  /* sample generators: 16 voices of two layers */

typedef struct
{
	int active;
	int chan, note, slot;               /* slot: the voice it belongs to */
	int held;                           /* key down (or kept by the sustain pedal) */
	int right;                          /* the layer's side */
	uint64_t age;

	uint32_t pos, end, loop;            /* sample word addresses */
	uint32_t frac;                      /* position fraction, 16 bits */
	uint32_t step;                      /* position step a frame, 16.16 */
	int root;                           /* the sample's key */
	int detune;                         /* 1/32 semitone */

	/* amplitude envelope: (rate, level) pairs, key-on half then release half */
	uint32_t env;                       /* ROM byte address of the entry */
	int seg, off;                       /* pair index, release half */
	int level;                          /* 0..0x7fff */
	int target, down, inc, every, count;
	int moving;

	/* pitch modulation */
	uint32_t pm;                        /* ROM byte address of the entry */
	int pm_seg, pm_wait, pm_left, pm_rate, pm_acc;
	int pm_val;                         /* 1/32 semitone */
} upd937_gen;

typedef struct
{
	int program;
	int bend;                           /* -128..127, 1/64 semitone */
	int sustain;
	int volume;                         /* 0..256 */
} upd937_chan;

typedef struct
{
	const uint8_t *rom;
	uint16_t ctrl;
	int midi_on;                        /* the mode slider is on MIDI */
	int channels;                       /* MIDI channels received: 1, 3 or 4 */
	uint8_t status, data[2];
	int ndata;
	upd937_chan ch[4];
	upd937_gen g[UPD937_GENS];
	uint64_t age;
	int tick;                           /* output frames to the next envelope step */
	int pm_tick;                        /* and to the next pitch modulation step */
	double lp[2][3];                    /* output filter state */
} upd937;

/* rom: the 512 KB dump as it comes (16-bit little-endian words); kept, not copied */
void upd937_init(upd937 *s, const uint8_t *rom);
void upd937_reset(upd937 *s);
/* SOUND_CTRL: the simulated buttons and sliders */
void upd937_ctrl(upd937 *s, uint16_t v);
void upd937_midi(upd937 *s, uint8_t b);
/* n stereo frames at UPD937_RATE */
void upd937_render(upd937 *s, int16_t *out, int n);

#endif
