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
	double audio_rate;           /* mono int16 samples per emulated second */

	/* a cartridge image (copied); 0 and a message in err if it cannot be used */
	int (*load)(const uint8_t *rom, size_t size, char *err, size_t errn);
	void (*reset)(void);
	void (*run_frame)(void);
	const uint32_t *(*framebuffer)(void);
	/* audio produced so far, up to max samples; returns how many */
	int (*audio_take)(int16_t *out, int max);
	void (*set_pad)(int pad, uint32_t buttons);
} core;

extern const core core_pv1000;

#endif
