/* Casiolator - what the frontend sees of an emulated system */
#ifndef CL_CORE_H
#define CL_CORE_H

#include <stddef.h>
#include <stdint.h>

/* the virtual pad every system maps its controls onto (bit = 1 << PAD_*) */
enum { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_C, PAD_D, PAD_L, PAD_R,
       PAD_START, PAD_SELECT, PAD_COUNT };

typedef struct core
{
	const char *name;            /* "Casio PV-1000" */
	const char *id;              /* "pv1000": config sections, screenshot names */
	int width, height;           /* framebuffer, 0x00RRGGBB */
	double aspect;               /* display aspect ratio of the whole framebuffer */
	double fps;
	double audio_rate;           /* int16 sample frames per emulated second */
	int audio_channels;          /* 1, or 2 for interleaved left and right */

	/* a cartridge image (copied); 0 and a message in err if it cannot be used */
	int (*load)(const uint8_t *rom, size_t size, char *err, size_t errn);
	void (*reset)(void);
	void (*run_frame)(void);
	const uint32_t *(*framebuffer)(void);
	/* audio produced so far, up to max frames; returns how many */
	int (*audio_take)(int16_t *out, int max);
	void (*set_pad)(int pad, uint32_t buttons);
	/* the system's BIOS (copied), NULL if it has none; 0 and a message if it is not one */
	int (*load_bios)(const uint8_t *data, size_t size, char *err, size_t errn);
	uint32_t bios_crc, bios_size;  /* the known dump */
	const char *bios_desc;         /* what to tell the user who has to find it */
	const char *const *bios_names; /* file names to look for, NULL-terminated */
	/* battery-backed RAM the frontend keeps in a file (NULL if none) */
	uint8_t *(*save_ram)(size_t *size);
	/* a second ROM the system can do without (the Loopy's sound samples), NULL if none;
	 * 0 and a message if the data is not it */
	int (*load_aux)(const uint8_t *data, size_t size, char *err, size_t errn);
	uint32_t aux_crc, aux_size;
	const char *aux_desc;
	const char *const *aux_names;
} core;

extern const core core_pv1000;
extern const core core_loopy;

#endif
