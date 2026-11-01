/* Casiolator - Casio PV-1000, PV-2000 and Loopy emulator: command line, window, sound, input
 *
 * casiolator [options] [cartridge]
 * With no cartridge the window opens on a start screen: drop a file on it or click to
 * browse. Zipped and plain images are both fine (see game.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "core.h"
#include "game.h"
#include "config.h"
#include "font.h"
#include "png.h"
#include "os.h"
#ifdef CL_HAVE_SDL
#include <SDL.h>
#endif

#ifndef CL_VERSION
#define CL_VERSION "dev"
#endif

static game s_game;
static const core *s_core;           /* the running system, NULL on the start screen */
static const char *s_bios_arg;       /* --bios */
static const char *s_aux_arg;        /* --samples */
static uint32_t s_script_pad[2];     /* scripted presses (--press) */

/* ---- scripted presses: "T:name[:dur][@pad],..." ---- */

static const char *const s_pad_names[PAD_COUNT] = {
	"up", "down", "left", "right", "a", "b", "c", "d", "l", "r", "start", "select"
};

typedef struct { double t, dur; int button, pad; } press;
static press s_press[64];
static int s_npress;

static int press_parse(const char *spec)
{
	const char *p = spec;
	while (*p && s_npress < 64)
	{
		press *e = &s_press[s_npress];
		char name[16] = "";
		int used = 0;
		e->dur = 0.2;
		e->pad = 0;
		if (sscanf(p, "%lf:%15[a-z]%n", &e->t, name, &used) < 2)
			return 0;
		p += used;
		if (*p == ':')
		{
			p++;
			e->dur = strtod(p, (char **)&p);
		}
		if (*p == '@')
		{
			p++;
			e->pad = (int)strtol(p, (char **)&p, 10) - 1;
		}
		e->button = -1;
		for (int b = 0; b < PAD_COUNT; b++)
			if (!strcmp(name, s_pad_names[b]))
				e->button = b;
		if (e->button < 0 || e->pad < 0 || e->pad > 1)
			return 0;
		s_npress++;
		if (*p == ',')
			p++;
	}
	return 1;
}

static void press_update(double t)
{
	s_script_pad[0] = s_script_pad[1] = 0;
	for (int i = 0; i < s_npress; i++)
		if (t >= s_press[i].t && t < s_press[i].t + s_press[i].dur)
			s_script_pad[s_press[i].pad] |= 1u << s_press[i].button;
}

/* ---- helpers ---- */

static void report(const char *fmt, ...)
{
	char msg[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", msg);
}

/* the window asks for a ROM file (NULL headless): 1 and the path, 0 if cancelled */
static int (*s_ask_bios)(const core *c, const char *why, char *out, size_t n);
static const core *s_bios_core;      /* the system whose BIOS is loaded */
static const core *s_aux_core;       /* the system whose second ROM was looked for */

/* a ROM a system needs besides the game: the BIOS, or the second ROM (aux) */
typedef struct
{
	uint32_t crc, size;
	const char *desc;
	const char *const *names;
	int (*load)(const uint8_t *data, size_t size, char *err, size_t errn);
	const char *arg;                 /* given on the command line */
	char *remember;                  /* the setting that keeps the choice */
	size_t remember_n;
} rom_need;

static int try_rom(const rom_need *r, const char *path, char *err, size_t errn)
{
	uint8_t *b;
	size_t n;
	const int res = bios_open(path, r->crc, r->size, &b, &n, err, errn);
	if (!res)
		return 0;
	const int ok = r->load(b, n, err, errn);
	free(b);
	if (ok && res == 2)
		report("%s: not the known dump (CRC differs), trying it anyway", path);
	return ok;
}

/* a path's folder ("." if it has none, ".." for ".") */
static void strip_name(char *path, size_t n)
{
	char *slash = NULL;
	for (char *q = path; *q; q++)
		if (*q == '/' || *q == '\\')
			slash = q;
	if (slash)
		*slash = 0;
	else
		snprintf(path, n, strcmp(path, ".") ? "." : "..");
}

/* the setting that remembers a system's BIOS */
static char *bios_setting(const core *c, size_t *n)
{
	if (c == &core_pv2000)
		return *n = sizeof(g_config.pv2000_bios), g_config.pv2000_bios;
	return *n = sizeof(g_config.loopy_bios), g_config.loopy_bios;
}

/* the command line, the remembered file, the usual names beside the executable, the game
 * or the BIOS, and last the user (the choice is remembered) */
static int find_rom(const core *c, const rom_need *r, const char *game_path, char *err, size_t errn)
{
	char dirs[4][1024], p[2200], e2[512];
	if (r->arg)
		return try_rom(r, r->arg, err, errn);
	if (r->remember[0] && try_rom(r, r->remember, e2, sizeof(e2)))
		return 1;
	/* beside the executable, the game, the game's folder (a game on two chips is a folder
	 * of its own) and the BIOS */
	os_exe_dir(dirs[0], sizeof(dirs[0]));
	snprintf(dirs[1], sizeof(dirs[1]), "%s", game_path);
	strip_name(dirs[1], sizeof(dirs[1]));
	snprintf(dirs[2], sizeof(dirs[2]), "%s", dirs[1]);
	strip_name(dirs[2], sizeof(dirs[2]));
	size_t bn;
	snprintf(dirs[3], sizeof(dirs[3]), "%s", bios_setting(c, &bn));
	strip_name(dirs[3], sizeof(dirs[3]));
	for (int d = 0; d < 4; d++)
		for (int k = 0; r->names[k]; k++)
		{
			snprintf(p, sizeof(p), "%.1000s/%s", dirs[d], r->names[k]);
			FILE *f = os_fopen(p, "rb");
			if (!f)
				continue;
			fclose(f);
			if (try_rom(r, p, e2, sizeof(e2)))
			{
				snprintf(r->remember, r->remember_n, "%.1000s", p);
				return 1;
			}
		}
	if (!s_ask_bios)
	{
		snprintf(err, errn, "%s: file not found", c->name);
		return 0;
	}
	const char *why = r->desc;
	for (;;)
	{
		if (!s_ask_bios(c, why, p, sizeof(p)))
		{
			snprintf(err, errn, "%s: no file chosen", c->name);
			return 0;
		}
		if (try_rom(r, p, e2, sizeof(e2)))
			break;
		why = e2;
	}
	snprintf(r->remember, r->remember_n, "%.1000s", p);
	return 1;
}

/* the system's BIOS, which it cannot do without */
static int ensure_bios(const core *c, const char *game_path, char *err, size_t errn)
{
	if (!c->load_bios || s_bios_core == c)
		return 1;
	size_t bn;
	char *remember = bios_setting(c, &bn);
	const rom_need r = { c->bios_crc, c->bios_size, c->bios_desc, c->bios_names, c->load_bios,
		s_bios_arg, remember, bn };
	if (!find_rom(c, &r, game_path, err, errn))
	{
		if (!s_bios_arg && !s_ask_bios)
			snprintf(err, errn, "%s needs its BIOS: use --bios", c->name);
		return 0;
	}
	s_bios_core = c;
	return 1;
}

/* the second ROM: looked for once; without it the system still runs (a message says so) */
static void ensure_aux(const core *c, const char *game_path)
{
	if (!c->load_aux || s_aux_core == c)
		return;
	s_aux_core = c;
	char err[512];
	const rom_need r = { c->aux_crc, c->aux_size, c->aux_desc, c->aux_names, c->load_aux,
		s_aux_arg, g_config.loopy_sound, sizeof(g_config.loopy_sound) };
	if (!find_rom(c, &r, game_path, err, sizeof(err)))
	{
		report("%s", c->aux_desc);
		report("%s", err);
	}
}

/* ---- battery saves: saves/<title>.sav beside the executable ---- */

static uint8_t s_saved[0x10000];     /* what the file holds, to write only on changes */

static void save_path(char *out, size_t n)
{
	char dir[1024];
	os_data_dir(dir, sizeof(dir));
	snprintf(out, n, "%s/saves/%s.sav", dir, s_game.title);
}

static void save_load(void)
{
	size_t size;
	uint8_t *ram = s_core && s_core->save_ram ? s_core->save_ram(&size) : NULL;
	if (!ram || size > sizeof(s_saved))
		return;
	char path[1400];
	save_path(path, sizeof(path));
	FILE *f = os_fopen(path, "rb");
	if (f)
	{
		if (fread(ram, 1, size, f) != size)
			report("%s: shorter than the cartridge RAM", path);
		fclose(f);
	}
	memcpy(s_saved, ram, size);
}

/* write the battery RAM if it changed */
static void save_flush(void)
{
	size_t size;
	uint8_t *ram = s_core && s_core->save_ram ? s_core->save_ram(&size) : NULL;
	if (!ram || size > sizeof(s_saved) || !memcmp(ram, s_saved, size))
		return;
	char dir[1024], path[1400];
	os_data_dir(dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/saves", dir);
	os_mkdir(path);
	save_path(path, sizeof(path));
	FILE *f = os_fopen(path, "wb");
	if (!f)
	{
		report("cannot write %s", path);
		return;
	}
	fwrite(ram, 1, size, f);
	fclose(f);
	memcpy(s_saved, ram, size);
}

static int load_game(const char *path, char *err, size_t errn)
{
	game g;
	if (!game_open(path, &g, err, errn))
		return 0;
	if (!ensure_bios(g.sys, path, err, errn))
	{
		game_free(&g);
		return 0;
	}
	ensure_aux(g.sys, path);
	save_flush();                        /* the game being left, before the core is reused */
	if (!g.sys->load(g.data, g.size, err, errn))
	{
		game_free(&g);
		return 0;
	}
	game_free(&s_game);
	s_game = g;
	s_core = g.sys;
	save_load();
	return 1;
}

static void write_wav_header(FILE *f, uint32_t rate, int ch, uint32_t frames)
{
	uint8_t h[44];
	uint32_t data = frames * 2 * ch;
	memcpy(h, "RIFF", 4);
	uint32_t v[] = { 36 + data };
	memcpy(h + 4, v, 4);
	memcpy(h + 8, "WAVEfmt ", 8);
	uint32_t fmt[] = { 16, 1 | (uint32_t)ch << 16, rate, rate * 2 * ch, 2 * ch | (16 << 16) };
	memcpy(h + 16, fmt, 20);
	memcpy(h + 36, "data", 4);
	memcpy(h + 40, &data, 4);
	fwrite(h, 1, 44, f);
}

/* ---- headless: run frames, then a screenshot and/or a WAV ---- */

static int run_headless(long frames, const char *shot, const char *wav)
{
	FILE *wf = NULL;
	uint32_t wn = 0;
	if (wav && !(wf = os_fopen(wav, "wb")))
	{
		report("cannot write %s", wav);
		return 1;
	}
	const int ch = s_core->audio_channels;
	if (wf)
		write_wav_header(wf, (uint32_t)(s_core->audio_rate + 0.5), ch, 0);
	int16_t abuf[8192];
	for (long f = 0; f < frames; f++)
	{
		press_update(f / s_core->fps);
		s_core->set_pad(0, s_script_pad[0]);
		s_core->set_pad(1, s_script_pad[1]);
		s_core->run_frame();
		for (int n; (n = s_core->audio_take(abuf, 4096)) > 0; wn += n)
			if (wf)
				fwrite(abuf, 2 * ch, n, wf);
	}
	if (wf)
	{
		fseek(wf, 0, SEEK_SET);
		write_wav_header(wf, (uint32_t)(s_core->audio_rate + 0.5), ch, wn);
		fclose(wf);
	}
	save_flush();
	if (shot && !png_write(shot, s_core->framebuffer(), s_core->width, s_core->width, s_core->height))
	{
		report("cannot write %s", shot);
		return 1;
	}
	return 0;
}

#ifdef CL_HAVE_SDL

/* ---- window frontend ---- */

#define OV_W 640
#define OV_H 480
#define OUT_RATE 48000

static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex, *s_otex;
static SDL_AudioDeviceID s_audio;
static uint32_t s_overlay[OV_W * OV_H];
static int s_paused, s_fullscreen, s_quit;
static char s_toast[256];
static double s_toast_until;
static uint32_t s_kbd;                       /* keyboard buttons, see kbd_player */
static SDL_GameController *s_pad[2];

static double now_s(void)
{
	return (double)SDL_GetTicks() / 1000.0;
}

static void toast(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_toast, sizeof(s_toast), fmt, ap);
	va_end(ap);
	s_toast_until = now_s() + 3.0;
	fprintf(stderr, "%s\n", s_toast);
}

static void set_title(void)
{
	char t[400];
	if (s_core)
		snprintf(t, sizeof(t), "%s - %s - Casiolator", s_game.title, s_core->name);
	else
		snprintf(t, sizeof(t), "Casiolator");
	SDL_SetWindowTitle(s_win, t);
}

/* the window size for the current system at scale k */
static void set_scale(int k)
{
	if (s_fullscreen)
		return;
	int h = s_core ? s_core->height * k : 240 * k;
	double aspect = s_core ? s_core->aspect : 4.0 / 3.0;
	SDL_DisplayMode dm;
	if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(s_win), &dm) == 0)
		while (k > 1 && (h > dm.h - 80 || (int)(h * aspect) > dm.w - 40))
			h = (s_core ? s_core->height : 240) * --k;
	SDL_SetWindowSize(s_win, (int)(h * aspect + 0.5), h);
	SDL_SetWindowPosition(s_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	g_config.scale = k;
}

static void start_game(void)
{
	if (s_tex)
		SDL_DestroyTexture(s_tex);
	s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
		s_core->width, s_core->height);
	if (s_audio)
		SDL_ClearQueuedAudio(s_audio);
	s_paused = 0;
	set_title();
	set_scale(g_config.scale);
}

static void remember_folder(const char *path)
{
	snprintf(g_config.game_dir, sizeof(g_config.game_dir), "%s", path);
	char *slash = NULL;
	for (char *c = g_config.game_dir; *c; c++)
		if (*c == '/' || *c == '\\')
			slash = c;
	if (slash)
		*slash = 0;
}

static void open_path(const char *path)
{
	char err[512];
	if (load_game(path, err, sizeof(err)))
	{
		remember_folder(path);
		start_game();
		toast("%s", s_game.title);
	}
	else
	{
		remember_folder(path);
		toast("%s", err);
	}
}

/* the first Loopy game with no BIOS around: say what is needed, then the file dialog */
static int ask_bios(const core *c, const char *why, char *out, size_t n)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, c->name, why, s_win);
	const int ok = os_pick_file(s_win, OS_PICK_BIOS, g_config.game_dir, out, n);
	SDL_RaiseWindow(s_win);
	return ok;
}

static void open_dialog(void)
{
	char path[4096];
	if (os_pick_file(s_win, OS_PICK_GAME, g_config.game_dir, path, sizeof(path)))
		open_path(path);
}

static void screenshot(void)
{
	if (!s_core)
		return;
	char dir[1024], path[1400];
	os_data_dir(dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/screenshots", dir);
	os_mkdir(path);
	for (int i = 1; i < 10000; i++)
	{
		snprintf(path, sizeof(path), "%s/screenshots/%s-%04d.png", dir, s_game.title, i);
		FILE *f = os_fopen(path, "rb");
		if (f)
		{
			fclose(f);
			continue;
		}
		if (png_write(path, s_core->framebuffer(), s_core->width, s_core->width, s_core->height))
			toast("screenshot %s-%04d.png", s_game.title, i);
		else
			toast("cannot write the screenshot");
		return;
	}
}

static void toggle_fullscreen(void)
{
	s_fullscreen = !s_fullscreen;
	SDL_SetWindowFullscreen(s_win, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	SDL_ShowCursor(s_fullscreen ? SDL_DISABLE : SDL_ENABLE);
}

/* ---- input ---- */

static int key_button(SDL_Keycode k)
{
	switch (k)
	{
	case SDLK_UP: return PAD_UP;
	case SDLK_DOWN: return PAD_DOWN;
	case SDLK_LEFT: return PAD_LEFT;
	case SDLK_RIGHT: return PAD_RIGHT;
	case SDLK_z: return PAD_A;
	case SDLK_x: return PAD_B;
	case SDLK_a: return PAD_C;
	case SDLK_s: return PAD_D;
	case SDLK_q: return PAD_L;
	case SDLK_w: return PAD_R;
	case SDLK_RETURN: return PAD_START;
	case SDLK_RSHIFT: case SDLK_BACKSPACE: return PAD_SELECT;
	default: return -1;
	}
}

static uint32_t pad_buttons(SDL_GameController *c)
{
	static const struct { int sdl, pad; } map[] = {
		{ SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT },
		{ SDL_CONTROLLER_BUTTON_A, PAD_A }, { SDL_CONTROLLER_BUTTON_B, PAD_B },
		{ SDL_CONTROLLER_BUTTON_X, PAD_C }, { SDL_CONTROLLER_BUTTON_Y, PAD_D },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R },
		{ SDL_CONTROLLER_BUTTON_START, PAD_START }, { SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT },
	};
	if (!c)
		return 0;
	uint32_t b = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (SDL_GameControllerGetButton(c, map[i].sdl))
			b |= 1u << map[i].pad;
	const int dead = 12000;
	const int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
	const int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
	if (x < -dead) b |= 1u << PAD_LEFT;
	if (x > dead) b |= 1u << PAD_RIGHT;
	if (y < -dead) b |= 1u << PAD_UP;
	if (y > dead) b |= 1u << PAD_DOWN;
	/* the triggers as L and R too */
	if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) b |= 1u << PAD_L;
	if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) b |= 1u << PAD_R;
	return b;
}

/* the keyboard plays the first player without a controller (player 1 if both have one) */
static int kbd_player(void)
{
	return !s_pad[0] ? 0 : !s_pad[1] ? 1 : 0;
}

static void controller_added(int device)
{
	for (int p = 0; p < 2; p++)
	{
		if (s_pad[p])
		{
			if (SDL_JoystickGetDeviceInstanceID(device) ==
				SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad[p])))
				return;                              /* already open */
			continue;
		}
		if ((s_pad[p] = SDL_GameControllerOpen(device)))
		{
			s_kbd = 0;
			toast("player %d: %s - keyboard: player %d", p + 1, SDL_GameControllerName(s_pad[p]), kbd_player() + 1);
		}
		return;
	}
}

static void controller_removed(SDL_JoystickID id)
{
	for (int p = 0; p < 2; p++)
		if (s_pad[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad[p])) == id)
		{
			SDL_GameControllerClose(s_pad[p]);
			s_pad[p] = NULL;
			s_kbd = 0;
			toast("player %d: controller disconnected - keyboard: player %d", p + 1, kbd_player() + 1);
		}
}

/* ---- sound: the core's rate to 48 kHz stereo, linear interpolation ---- */

static double s_rs_pos;
static int s_rs_prev[2];

static void queue_audio(void)
{
	static int16_t in[8192], out[16384];
	const double step = s_core->audio_rate / OUT_RATE;
	const int gain = g_config.volume, ch = s_core->audio_channels;
	for (int n; (n = s_core->audio_take(in, 4096)) > 0; )
	{
		int o = 0;
		for (int i = 0; i < n; i++)
		{
			const int l = in[i * ch], r = in[i * ch + ch - 1];
			while (s_rs_pos < 1.0 && o < 16384)
			{
				out[o++] = (int16_t)((s_rs_prev[0] + (l - s_rs_prev[0]) * s_rs_pos) * gain / 100);
				out[o++] = (int16_t)((s_rs_prev[1] + (r - s_rs_prev[1]) * s_rs_pos) * gain / 100);
				s_rs_pos += step;
			}
			s_rs_pos -= 1.0;
			s_rs_prev[0] = l;
			s_rs_prev[1] = r;
		}
		if (s_audio)
			SDL_QueueAudio(s_audio, out, o * 2);
	}
}

/* ---- drawing ---- */

/* the game picture: as big as fits, at the system's aspect ratio */
static SDL_Rect game_rect(void)
{
	int ww, wh;
	SDL_GetRendererOutputSize(s_ren, &ww, &wh);
	const double aspect = s_core ? s_core->aspect : 4.0 / 3.0;
	SDL_Rect r;
	if (ww > wh * aspect)
	{
		r.h = wh;
		r.w = (int)(wh * aspect + 0.5);
	}
	else
	{
		r.w = ww;
		r.h = (int)(ww / aspect + 0.5);
	}
	r.x = (ww - r.w) / 2;
	r.y = (wh - r.h) / 2;
	return r;
}

static void text_center(int y, const char *s, uint32_t color, int scale)
{
	const int w = (int)strlen(s) * FONT_W * scale;
	font_draw(s_overlay, OV_W, OV_W, OV_H, (OV_W - w) / 2, y, s, color, scale);
}

static int draw_overlay(void)
{
	int drawn = 0;
	memset(s_overlay, 0, sizeof(s_overlay));
	if (!s_core)
	{
		fill_rect(s_overlay, OV_W, OV_W, OV_H, 0, 0, OV_W, OV_H, 0xff10141c);
		text_center(150, "CASIOLATOR", 0xffffd060, 4);
		text_center(215, "Casio PV-1000, PV-2000 and Loopy", 0xffc0c8d8, 2);
		text_center(290, "Drop a cartridge on this window (.bin or .zip)", 0xffffffff, 1);
		text_center(310, "or click anywhere to look for one", 0xffffffff, 1);
		text_center(450, "version " CL_VERSION, 0xff607080, 1);
		drawn = 1;
	}
	else if (s_paused)
	{
		fill_rect(s_overlay, OV_W, OV_W, OV_H, 0, 0, OV_W, OV_H, 0x60000000);
		text_center(225, "PAUSED", 0xffffffff, 3);
		drawn = 1;
	}
	if (s_toast[0] && now_s() < s_toast_until)
	{
		const int w = (int)strlen(s_toast) * FONT_W * 2 + 16;
		const int x = w < OV_W ? (OV_W - w) / 2 : 0;
		fill_rect(s_overlay, OV_W, OV_W, OV_H, x, OV_H - 40, w, 28, 0xc0000000);
		font_draw(s_overlay, OV_W, OV_W, OV_H, x + 8, OV_H - 34, s_toast, 0xffffffff, 2);
		drawn = 1;
	}
	return drawn;
}

static void present(void)
{
	SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
	SDL_RenderClear(s_ren);
	const SDL_Rect r = game_rect();
	if (s_core && s_tex)
	{
		SDL_UpdateTexture(s_tex, NULL, s_core->framebuffer(), s_core->width * 4);
		SDL_RenderCopy(s_ren, s_tex, NULL, &r);
	}
	if (draw_overlay())
	{
		SDL_UpdateTexture(s_otex, NULL, s_overlay, OV_W * 4);
		SDL_RenderCopy(s_ren, s_otex, NULL, &r);
	}
	SDL_RenderPresent(s_ren);
}

static void key_event(const SDL_KeyboardEvent *k)
{
	const int down = k->type == SDL_KEYDOWN;
	const SDL_Keycode sym = k->keysym.sym;
	const int alt = (k->keysym.mod & KMOD_ALT) != 0, ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
	if (down && !k->repeat)
	{
		if (sym == SDLK_F11 || (sym == SDLK_RETURN && alt)) { toggle_fullscreen(); return; }
		if (sym == SDLK_o && ctrl) { open_dialog(); return; }
		if (sym == SDLK_F5 && s_core) { s_core->reset(); toast("reset"); return; }
		if (sym == SDLK_F12) { screenshot(); return; }
		if ((sym == SDLK_PAUSE || sym == SDLK_p) && s_core) { s_paused = !s_paused; return; }
		if (sym == SDLK_ESCAPE)
		{
			if (s_fullscreen) toggle_fullscreen();
			else s_quit = 1;
			return;
		}
		if ((sym == SDLK_EQUALS || sym == SDLK_PLUS || sym == SDLK_KP_PLUS) && ctrl) { set_scale(g_config.scale + 1); return; }
		if ((sym == SDLK_MINUS || sym == SDLK_KP_MINUS) && ctrl && g_config.scale > 1) { set_scale(g_config.scale - 1); return; }
	}
	if (sym == SDLK_RETURN && alt)
		return;
	const int b = key_button(sym);
	if (b >= 0)
	{
		if (down) s_kbd |= 1u << b;
		else s_kbd &= ~(1u << b);
	}
}

static int run_window(int fullscreen, const char *path)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		report("SDL_Init: %s", SDL_GetError());
		return 1;
	}
	char db[1200], dir[1024];
	os_exe_dir(dir, sizeof(dir));
	snprintf(db, sizeof(db), "%s/gamecontrollerdb.txt", dir);
	SDL_GameControllerAddMappingsFromFile(db);

	s_win = SDL_CreateWindow("Casiolator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!s_ren)
		s_ren = SDL_CreateRenderer(s_win, -1, 0);
	s_otex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, OV_W, OV_H);
	SDL_SetTextureBlendMode(s_otex, SDL_BLENDMODE_BLEND);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

	SDL_AudioSpec want = { 0 }, have;
	want.freq = OUT_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	s_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (s_audio)
		SDL_PauseAudioDevice(s_audio, 0);
	else
		report("no audio: %s", SDL_GetError());

	set_title();
	SDL_ShowWindow(s_win);
	s_ask_bios = ask_bios;
	if (path)
		open_path(path);
	if (fullscreen)
		toggle_fullscreen();

	const double freq = (double)SDL_GetPerformanceFrequency();
	uint64_t t0 = SDL_GetPerformanceCounter();
	long frames = 0;
	while (!s_quit)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			switch (ev.type)
			{
			case SDL_QUIT:
				s_quit = 1;
				break;
			case SDL_DROPFILE:
				open_path(ev.drop.file);
				SDL_free(ev.drop.file);
				t0 = SDL_GetPerformanceCounter();
				frames = 0;
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (!s_core && ev.button.button == SDL_BUTTON_LEFT)
				{
					open_dialog();
					t0 = SDL_GetPerformanceCounter();
					frames = 0;
				}
				break;
			case SDL_CONTROLLERDEVICEADDED:
				controller_added(ev.cdevice.which);
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				controller_removed(ev.cdevice.which);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				key_event(&ev.key);
				break;
			}
		}

		if (s_core && !s_paused)
		{
			const int kp = kbd_player();
			s_core->set_pad(0, (kp == 0 ? s_kbd : 0) | pad_buttons(s_pad[0]));
			s_core->set_pad(1, (kp == 1 ? s_kbd : 0) | pad_buttons(s_pad[1]));
			if (s_audio)
			{
				/* the sound card is the clock: emulate until 50 ms of sound wait in the queue */
				const uint32_t low = OUT_RATE * 4 * 50 / 1000;   /* 16-bit stereo */
				for (int i = 0; i < 6 && SDL_GetQueuedAudioSize(s_audio) < low; i++)
				{
					s_core->run_frame();
					queue_audio();
				}
			}
			else
			{
				/* no sound: pace on the host clock */
				const double host = (double)(SDL_GetPerformanceCounter() - t0) / freq;
				for (int i = 0; i < 6 && frames < host * s_core->fps; i++, frames++)
				{
					s_core->run_frame();
					queue_audio();
				}
			}
		}
		else
		{
			t0 = SDL_GetPerformanceCounter();
			frames = 0;
		}
		present();

		static uint32_t last_flush;
		if (SDL_GetTicks() - last_flush > 5000)
		{
			last_flush = SDL_GetTicks();
			save_flush();
		}
	}
	save_flush();

	if (s_audio)
		SDL_CloseAudioDevice(s_audio);
	for (int p = 0; p < 2; p++)
		if (s_pad[p])
			SDL_GameControllerClose(s_pad[p]);
	SDL_DestroyRenderer(s_ren);
	SDL_DestroyWindow(s_win);
	SDL_Quit();
	config_save();
	return 0;
}
#endif

static void usage(void)
{
	printf(
		"Casiolator " CL_VERSION " - Casio PV-1000, PV-2000 and Loopy emulator\n"
		"usage: casiolator [options] [cartridge.bin|.zip]\n"
		"  --fullscreen       start in full screen\n"
		"  --headless         no window, no sound (with --frames)\n"
		"  --frames N         stop after N frames\n"
		"  --screenshot F     PNG of the last frame (headless)\n"
		"  --wav F            the sound (headless)\n"
		"  --bios F           the system's BIOS (Loopy: hd6437021.lsi302 or casloopy.zip;\n"
		"                     PV-2000: hn613128pc64.bin or pv2000.zip)\n"
		"  --samples F        the Loopy's sound sample ROM (hn62434fa.lsi352 or casloopy.zip)\n"
		"  --press SPEC       scripted presses, \"T:button[:dur][@pad],...\" (T, dur in seconds)\n"
		"                     buttons: up down left right a b c d l r start select\n");
}

int main(int argc, char **argv)
{
	os_utf8_args(&argc, &argv);
	const char *path = NULL, *shot = NULL, *wav = NULL;
	long frames = 0;
	int headless = 0, fullscreen = 0;
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp(a, "--headless")) headless = 1;
		else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
		else if (!strcmp(a, "--frames") && i + 1 < argc) frames = atol(argv[++i]);
		else if (!strcmp(a, "--screenshot") && i + 1 < argc) shot = argv[++i];
		else if (!strcmp(a, "--wav") && i + 1 < argc) wav = argv[++i];
		else if (!strcmp(a, "--bios") && i + 1 < argc) s_bios_arg = argv[++i];
		else if (!strcmp(a, "--samples") && i + 1 < argc) s_aux_arg = argv[++i];
		else if (!strcmp(a, "--press") && i + 1 < argc)
		{
			if (!press_parse(argv[++i]))
			{
				os_console_attach();
				report("bad --press: %s", argv[i]);
				return 2;
			}
		}
		else if (!strcmp(a, "--help") || !strcmp(a, "-h"))
		{
			os_console_attach();
			usage();
			return 0;
		}
		else if (a[0] == '-' && a[1] == '-')
		{
			os_console_attach();
			report("unknown option %s", a);
			return 2;
		}
		else path = a;
	}

	char err[512];
	if (headless)
	{
		os_console_attach();
		if (!path || !frames)
		{
			report("--headless needs a cartridge and --frames");
			return 2;
		}
		if (!load_game(path, err, sizeof(err)))
		{
			report("%s", err);
			return 1;
		}
		return run_headless(frames, shot, wav);
	}

#ifdef CL_HAVE_SDL
	os_console_attach();
	config_load();
	return run_window(fullscreen, path);
#else
	report("built without SDL: use --headless");
	return 2;
#endif
}
