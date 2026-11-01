/* Casiolator - the few host services the frontend needs (kept apart from windows.h) */
#ifndef CL_OS_H
#define CL_OS_H

#include <stddef.h>
#include <stdio.h>

struct SDL_Window;

/* Paths are UTF-8 everywhere in Casiolator (SDL hands dropped files over in UTF-8); on Windows
 * these go through the wide-character API so that any file name works. */

/* replace argv with UTF-8 copies of the command line's arguments (Windows; else no-op) */
void os_utf8_args(int *argc, char ***argv);
/* fopen for a UTF-8 path */
FILE *os_fopen(const char *path, const char *mode);

/* 64-bit file positions */
int os_fseek(FILE *f, long long off, int whence);
long long os_ftell(FILE *f);
/* folder of the executable, without a trailing separator ("." if unknown) */
void os_exe_dir(char *out, size_t n);
/* where casiolator.ini goes, and where screenshots/ and the rest go (created if missing).
 * Windows, and a Linux build unpacked in a writable folder: next to the executable.
 * Otherwise (installed, AppImage): $XDG_CONFIG_HOME/casiolator and $XDG_DATA_HOME/casiolator. */
void os_config_dir(char *out, size_t n);
void os_data_dir(char *out, size_t n);
/* 1 if os_pick_file can show a dialog (Windows; Linux with zenity or kdialog) */
int os_can_pick(void);
/* the GUI build started from a terminal: print to its console (output redirected to a
 * file or a pipe stays as it is); 0 if stderr goes nowhere (started by a double click) */
int os_console_attach(void);
/* create a folder (no error if it exists) */
void os_mkdir(const char *path);
enum { OS_PICK_GAME, OS_PICK_BIOS };
/* native "open a file" dialog for a cartridge or the BIOS, starting in folder dir (NULL or "":
 * the system's choice); 1 and the path in out, 0 if cancelled or unavailable */
int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n);

#endif
