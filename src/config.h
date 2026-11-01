/* Casiolator - settings kept in casiolator.ini next to the executable
 *
 * One "key = value" per line, '#' or ';' starts a comment. The window frontend reads it at
 * start and writes it back on exit, with what was chosen in the window. */
#ifndef CL_CONFIG_H
#define CL_CONFIG_H

typedef struct
{
	char game_dir[1024];      /* folder of the last cartridge opened, for the dialog */
	char loopy_bios[1024];    /* the Loopy BIOS file (or a zip with it) */
	char loopy_sound[1024];   /* the Loopy sound sample ROM (or a zip with it) */
	char pv2000_bios[1024];   /* the PV-2000 BIOS file (or a zip with it) */
	int volume;               /* 0..100 */
	int scale;                /* window height as a multiple of the system's lines */
} config;

extern config g_config;

int config_load(void);
int config_save(void);

#endif
