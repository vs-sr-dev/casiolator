/* Casiolator - the Loopy's sound chip (NEC CDT109, a cost-reduced uPD937)
 *
 * Clean room: written from kasami's documentation (MIDI subset, modes, SOUND_CTRL) and
 * our own reading of the sample ROM (docs/loopy-sound.md); no other emulator's code was
 * read. What the ROM does not say (envelope and modulation time base, interpolation) is
 * a choice, marked as such below, to be checked against the real console.
 *
 * The sample ROM (16-bit little-endian words; addresses here are byte offsets):
 *   0x1000 envelope rates: 256 x (step, wait-1 | key << 8): the level moves by `step`
 *          every `wait` ticks; 0x7fff jumps
 *   0x1400 level to amplitude: 256 words, about 0.25 dB a step
 *   0x1600 pitch: 0x800 * 2^(i / 384), 1/32 semitone a step over four octaves; 0x800
 *          plays a sample one sample every 16 output frames
 *   0x2200 programs: 112 x (first zone * 6, split map | 0x1000 two zones a key range)
 *   0x3da0 split maps: 32 bytes, one nibble per key from 36 (the key range)
 *   0x4360 zones: 2 layers x (pitch modulation, envelope, sample)
 *   0x6220 pitch modulation: 32 bytes, (detune, delay, (speed, distance)..., 0x20nn loop)
 *   0x6d40 envelopes: 64 bytes, 16 (rate | 0x80 down, level) pairs for the key held,
 *          16 more for after its release, followed from the same pair index
 *   0xa640 samples: 405 x 10 bytes (key, start, end, loop), 24-bit word addresses
 *   0xb612.. sample data: 12-bit offset binary in the high bits of each word */
#include <string.h>
#include "upd937.h"

#define R_RATES   0x1000
#define R_LEVELS  0x1400
#define R_PITCH   0x1600
#define R_PROGS   0x2200
#define R_SPLITS  0x3da0
#define R_ZONES   0x4360
#define R_PMOD    0x6220
#define R_ENVS    0x6d40
#define R_SAMPLES 0xa640
#define PROGRAMS  110

/* choices, not facts (see above): the envelope steps every ENV_TICK output frames; pitch
 * modulation runs every PM_TICK frames, speed 1 moving about 7.5 units a second (vibratos
 * then come out at 5-6 Hz), its delay in units of about 12 ms */
#define ENV_TICK  1
#define PM_TICK   64
#define PM_SPEED  371           /* 7.5 / (UPD937_RATE / PM_TICK) * 65536 */
#define PM_DELAY  16            /* PM_TICK periods */

static const int s_voices[3][4] = {
	{ 12, 0, 0, 0 },            /* one channel */
	{ 6, 4, 2, 0 },             /* three channels (the fourth is the rhythm part) */
	{ 6, 4, 2, 4 },             /* four channels */
};

static inline int rw(const upd937 *s, uint32_t a)
{
	return s->rom[a & (UPD937_ROM - 1)] | s->rom[(a + 1) & (UPD937_ROM - 1)] << 8;
}

static inline int sample_at(const upd937 *s, uint32_t word)
{
	return (rw(s, word * 2) >> 4) - 0x800;
}

static inline uint32_t r24(const upd937 *s, uint32_t a)
{
	return s->rom[a] | s->rom[a + 1] << 8 | (uint32_t)s->rom[a + 2] << 16;
}

/* sign and magnitude, sign in bit 12 */
static int sm12(int v)
{
	return (v & 0x1000) ? -(v & 0xfff) : (v & 0xfff);
}

void upd937_init(upd937 *s, const uint8_t *rom)
{
	memset(s, 0, sizeof(*s));
	s->rom = rom;
	upd937_reset(s);
}

void upd937_reset(upd937 *s)
{
	const uint8_t *rom = s->rom;
	const uint16_t ctrl = s->ctrl;
	memset(s, 0, sizeof(*s));
	s->rom = rom;
	s->ctrl = ctrl;
	s->channels = 1;
	for (int c = 0; c < 4; c++)
		s->ch[c].volume = 256;
}

/* ---- envelope ---- */

static void env_start_pair(upd937 *s, upd937_gen *g)
{
	for (;;)
	{
		if (g->seg >= 16)
		{
			g->moving = 0;
			if (g->level == 0)
				g->active = 0;
			return;
		}
		const uint32_t a = g->env + (g->off ? 32 : 0) + g->seg * 2;
		const int rate = s->rom[a], lvl = s->rom[a + 1];
		g->target = lvl << 7;
		g->down = rate >> 7;
		if (g->down ? g->level > g->target : g->level < g->target)
		{
			const int e = rw(s, R_RATES + (rate & 0x7f) * 8);
			const int w = rw(s, R_RATES + (rate & 0x7f) * 8 + 2);
			g->inc = e;
			g->every = (w & 0xff) + 1;
			g->count = g->every;
			g->moving = 1;
			if (g->inc == 0)
				g->moving = 0;              /* rate 0: stays here */
			return;
		}
		g->seg++;                           /* already there */
	}
}

static void env_step(upd937 *s, upd937_gen *g)
{
	if (!g->moving || --g->count > 0)
		return;
	g->count = g->every;
	if (g->down)
	{
		g->level -= g->inc;
		if (g->level > g->target)
			return;
	}
	else
	{
		g->level += g->inc;
		if (g->level < g->target)
			return;
	}
	g->level = g->target;
	g->seg++;
	env_start_pair(s, g);
}

static void gen_release(upd937 *s, upd937_gen *g)
{
	if (g->off)
		return;
	g->off = 1;
	env_start_pair(s, g);
}

/* ---- pitch modulation ---- */

static void pm_next(upd937 *s, upd937_gen *g)
{
	for (int guard = 0; guard < 8; guard++)
	{
		if (g->pm_seg >= 7)
			break;
		const uint32_t a = g->pm + 4 + g->pm_seg * 4;
		const int r = rw(s, a), t = rw(s, a + 2);
		if ((r & 0xff00) == 0x2000)
		{
			g->pm_seg = (r & 0xff) - 1;     /* loop to pair nn, counted from 1 */
			continue;
		}
		if (t == 0 || (r & 0xfff) == 0)
			break;                          /* the end, or speed 0: stays */
		g->pm_rate = sm12(r);
		g->pm_left = t;
		g->pm_seg++;
		return;
	}
	g->pm_rate = 0;
	g->pm_left = -1;                        /* finished */
}

static void pm_step(upd937 *s, upd937_gen *g)
{
	if (g->pm_wait > 0)
	{
		if (--g->pm_wait == 0)
			pm_next(s, g);
		return;
	}
	if (g->pm_left < 0)
		return;
	/* (speed, distance): `distance` units up or down (the speed's sign) at `speed` */
	g->pm_acc += (g->pm_rate < 0 ? -g->pm_rate : g->pm_rate) * PM_SPEED;
	while (g->pm_acc >= 0x10000 && g->pm_left > 0)
	{
		g->pm_acc -= 0x10000;
		g->pm_val += g->pm_rate < 0 ? -1 : 1;
		if (--g->pm_left == 0)
			pm_next(s, g);
	}
}

/* ---- pitch ---- */

/* the sample step (16.16) for a pitch in 1/256 of a table step above the sample's key */
static uint32_t step_for(const upd937 *s, int32_t p)
{
	int oct = 0;
	while (p < 0)
	{
		p += 384 * 256;
		oct--;
	}
	while (p >= 384 * 256)
	{
		p -= 384 * 256;
		oct++;
	}
	const int i = p >> 8, f = p & 0xff;
	const int a = rw(s, R_PITCH + i * 2);
	const int b = i < 383 ? rw(s, R_PITCH + i * 2 + 2) : a * 2;
	uint32_t v = (uint32_t)(a * 256 + (b - a) * f);   /* 0x800 * 256 = 1/16 */
	/* 16.16 step = table / 0x8000 */
	v >>= 7;
	return oct >= 0 ? v << oct : v >> -oct;
}

static uint32_t gen_step(const upd937 *s, const upd937_gen *g)
{
	const upd937_chan *c = &s->ch[g->chan];
	const int32_t p = ((g->note - g->root) * 32 + g->detune + g->pm_val) * 256 + c->bend * 128;
	return step_for(s, p);
}

/* ---- notes ---- */

static int voices_used(const upd937 *s, int chan, int *oldest_slot)
{
	int seen[UPD937_GENS], n = 0, used = 0;
	uint64_t best = ~0ull;
	int best_rel = -1;
	*oldest_slot = -1;
	for (int i = 0; i < UPD937_GENS; i++)
	{
		const upd937_gen *g = &s->g[i];
		if (!g->active || g->chan != chan)
			continue;
		int k;
		for (k = 0; k < n && seen[k] != g->slot; k++)
			;
		if (k < n)
			continue;
		seen[n++] = g->slot;
		used++;
		/* steal a released note before a held one, the oldest first */
		const int rel = g->off;
		if (rel > best_rel || (rel == best_rel && g->age < best))
		{
			best_rel = rel;
			best = g->age;
			*oldest_slot = g->slot;
		}
	}
	return used;
}

static void kill_slot(upd937 *s, int slot)
{
	for (int i = 0; i < UPD937_GENS; i++)
		if (s->g[i].active && s->g[i].slot == slot)
			s->g[i].active = 0;
}

static void note_off(upd937 *s, int chan, int note)
{
	for (int i = 0; i < UPD937_GENS; i++)
	{
		upd937_gen *g = &s->g[i];
		if (g->active && g->chan == chan && g->note == note && g->held)
		{
			g->held = 0;
			if (!s->ch[chan].sustain)
				gen_release(s, g);
		}
	}
}

static void note_on(upd937 *s, int chan, int note)
{
	const int prog = s->ch[chan].program;
	const int mode = s->channels == 4 ? 2 : s->channels == 3 ? 1 : 0;
	const int limit = s_voices[mode][chan];
	if (prog < 0 || prog >= PROGRAMS || !limit)
		return;
	while (note < 36)
		note += 12;
	while (note > 96)
		note -= 12;
	note_off(s, chan, note);            /* the same key again: release the old one */

	const int zfirst = rw(s, R_PROGS + prog * 4) / 6;
	const int split = rw(s, R_PROGS + prog * 4 + 2);
	const int two = (split & 0x1000) != 0;
	const int k = note - 36 < 60 ? note - 36 : 59;
	const int range = (s->rom[R_SPLITS + (split & 0xff) * 32 + k / 2] >> ((k & 1) * 4)) & 15;
	const int cost = two ? 2 : 1;

	/* make room: a voice is two generators, a two-zone note takes two voices */
	int victim;
	while (voices_used(s, chan, &victim) + cost > limit && victim >= 0)
		kill_slot(s, victim);
	if (cost > limit)
		return;

	const int slot = (int)(++s->age & 0x7fffffff);
	for (int z = 0; z < cost; z++)
	{
		const uint32_t za = R_ZONES + (uint32_t)(zfirst + range * cost + z) * 12;
		for (int layer = 0; layer < 2; layer++)
		{
			upd937_gen *g = NULL;
			for (int i = 0; i < UPD937_GENS && !g; i++)
				if (!s->g[i].active)
					g = &s->g[i];
			if (!g)
				return;
			const int pmi = rw(s, za + layer * 6), envi = rw(s, za + layer * 6 + 2);
			const int smp = rw(s, za + layer * 6 + 4);
			const uint32_t sa = R_SAMPLES + (uint32_t)smp * 10;
			memset(g, 0, sizeof(*g));
			g->active = 1;
			g->chan = chan;
			g->note = note;
			g->slot = slot;
			g->held = 1;
			g->right = layer;
			g->age = s->age;
			g->root = s->rom[sa];
			g->pos = r24(s, sa + 1);
			g->end = r24(s, sa + 4);
			g->loop = r24(s, sa + 7);
			g->env = R_ENVS + (uint32_t)envi * 64;
			g->pm = R_PMOD + (uint32_t)pmi * 32;
			g->detune = sm12(rw(s, g->pm));
			g->pm_wait = rw(s, g->pm + 2) * PM_DELAY;
			if (g->pm_wait == 0)
				pm_next(s, g);
			g->step = gen_step(s, g);
			env_start_pair(s, g);
		}
	}
}

/* ---- MIDI ---- */

static void message(upd937 *s)
{
	const int chan = s->status & 15, d0 = s->data[0], d1 = s->data[1];
	if (chan >= s->channels)
		return;
	upd937_chan *c = &s->ch[chan];
	switch (s->status & 0xf0)
	{
	case 0x80:
		note_off(s, chan, d0);
		break;
	case 0x90:
		if (d1)
			note_on(s, chan, d0);
		else
			note_off(s, chan, d0);
		break;
	case 0xb0:
		if (d0 == 0x40)
		{
			c->sustain = d1 >= 0x40;
			if (!c->sustain)
				for (int i = 0; i < UPD937_GENS; i++)
				{
					upd937_gen *g = &s->g[i];
					if (g->active && g->chan == chan && !g->held)
						gen_release(s, g);
				}
		}
		break;
	case 0xc0:
		/* a program change ends the channel's notes: the BIOS changes tracks with program
		 * changes and no note-offs, and kasami's track notes stop the music that way;
		 * values past the last program reset the channel without changing it */
		for (int i = 0; i < UPD937_GENS; i++)
		{
			upd937_gen *g = &s->g[i];
			if (g->active && g->chan == chan)
			{
				g->held = 0;
				gen_release(s, g);
			}
		}
		if (d0 < PROGRAMS)
			c->program = d0;
		else
		{
			c->sustain = 0;
			c->bend = 0;
		}
		break;
	case 0xe0:
		c->bend = ((d1 << 7 | d0) >> 6) - 128;
		for (int i = 0; i < UPD937_GENS; i++)
			if (s->g[i].active && s->g[i].chan == chan)
				s->g[i].step = gen_step(s, &s->g[i]);
		break;
	}
}

void upd937_midi(upd937 *s, uint8_t b)
{
	if (!s->midi_on)
		return;
	if (b >= 0xf8)
		return;                             /* real time: the rhythm part, not emulated */
	if (b >= 0x80)
	{
		s->status = b < 0xf0 ? b : 0;
		s->ndata = 0;
		return;
	}
	if (!s->status)
		return;
	s->data[s->ndata++] = b;
	const int need = ((s->status & 0xe0) == 0xc0) ? 1 : 2;
	if (s->ndata < need)
		return;
	s->ndata = 0;                           /* running status */
	message(s);
}

/* ---- SOUND_CTRL ---- */

void upd937_ctrl(upd937 *s, uint16_t v)
{
	const uint16_t rise = v & ~s->ctrl;
	s->ctrl = v;
	if (v & 0x10)
	{
		/* ON: the synthesizer starts over, not listening to MIDI */
		if (rise & 0x10)
		{
			upd937_reset(s);
			s->ctrl = v;
		}
		s->midi_on = 0;
		return;
	}
	if (v & 0x20)
		s->midi_on = 1;
	if (rise & 0x02)
		s->channels = 3;
	if (rise & 0x04)
		s->channels = 4;
	/* the volume sliders: 2, 3, 4 = 150, 200, 255 / 255 (kasami); channel 1 is fixed */
	static const int vol[8] = { 256, 151, 201, 256, 256, 256, 256, 256 };
	const int av = (v >> 6) & 7, rv = (v >> 9) & 7;
	const int a = av == 1 ? vol[1] : av == 2 ? vol[2] : 256;
	const int r = rv == 1 ? vol[1] : rv == 2 ? vol[2] : 256;
	s->ch[1].volume = s->ch[2].volume = a;
	s->ch[3].volume = r;
}

/* ---- output ---- */

void upd937_render(upd937 *s, int16_t *out, int n)
{
	for (int f = 0; f < n; f++)
	{
		const int env_now = --s->tick <= 0;
		if (env_now)
			s->tick = ENV_TICK;
		const int pm_now = ++s->pm_tick >= PM_TICK;
		if (pm_now)
			s->pm_tick = 0;
		int32_t mix[2] = { 0, 0 };
		for (int i = 0; i < UPD937_GENS; i++)
		{
			upd937_gen *g = &s->g[i];
			if (!g->active)
				continue;
			if (pm_now)
			{
				pm_step(s, g);
				g->step = gen_step(s, g);
			}
			/* linear interpolation between neighbouring samples (a choice) */
			const int a = sample_at(s, g->pos);
			const int b = sample_at(s, g->pos + 1 <= g->end ? g->pos + 1 : g->loop);
			const int v = a + (((b - a) * (int)(g->frac >> 4)) >> 12);
			const int amp = rw(s, R_LEVELS + (g->level >> 7) * 2);
			mix[g->right] += (v * amp >> 14) * s->ch[g->chan].volume >> 8;

			g->frac += g->step;
			g->pos += g->frac >> 16;
			g->frac &= 0xffff;
			while (g->pos > g->end)
				g->pos -= g->end + 1 - g->loop;
			if (env_now)
				env_step(s, g);
		}
		for (int c = 0; c < 2; c++)
		{
			/* the console's output filter: a third-order Sallen-Key low-pass, 10K 10K 10K,
			 * 330p 5n6 330p (kasami's Loopy-Schematics, Audio-Path), +4.8 dB at 7.5 kHz;
			 * bilinear transform at the chip's rate, warped to match at 7.5 kHz */
			static const double b0 = 0.0601007042, b1 = 0.1803021127;
			static const double a1 = -0.8285999962, a2 = -0.0969066256, a3 = 0.4063122558;
			double *z = s->lp[c];
			const double x = mix[c];
			const double y = b0 * x + z[0];
			z[0] = b1 * x - a1 * y + z[1];
			z[1] = b1 * x - a2 * y + z[2];
			z[2] = b0 * x - a3 * y;
			int32_t v = (int32_t)(y < 0 ? y - 0.5 : y + 0.5);
			if (v > 32767)
				v = 32767;
			if (v < -32768)
				v = -32768;
			out[f * 2 + c] = (int16_t)v;
		}
	}
}
