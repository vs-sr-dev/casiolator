/* Casiolator - host services (see os.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <direct.h>
#ifdef CL_HAVE_SDL
#include <SDL.h>
#include <SDL_syswm.h>
#endif

#define WPATH 4096

/* UTF-8 to UTF-16; 0 if s is not valid UTF-8 or does not fit */
static int widen(const char *s, wchar_t *w, int n)
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) > 0;
}

static void narrow(const wchar_t *w, char *s, size_t n)
{
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, s, (int)n, NULL, NULL) && n) s[0] = 0;
}

void os_utf8_args(int *argc, char ***argv)
{
	int n;
	wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
	if (!w) return;
	char **a = calloc((size_t)n + 1, sizeof(char *));
	for (int i = 0; a && i < n; i++)
	{
		const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
		if (!(a[i] = malloc(len > 0 ? len : 1))) return;
		narrow(w[i], a[i], len > 0 ? len : 1);
	}
	LocalFree(w);
	if (!a) return;
	*argc = n;
	*argv = a;                               /* kept for the whole run */
}

FILE *os_fopen(const char *path, const char *mode)
{
	wchar_t wp[WPATH], wm[16];
	/* not UTF-8 (a .cue written in the ANSI code page naming its .bin): as it is */
	if (!widen(path, wp, WPATH) || !widen(mode, wm, 16)) return fopen(path, mode);
	return _wfopen(wp, wm);
}

int os_fseek(FILE *f, long long off, int whence)
{
	return _fseeki64(f, off, whence);
}

long long os_ftell(FILE *f)
{
	return _ftelli64(f);
}

void os_config_dir(char *out, size_t n)
{
	os_exe_dir(out, n);
}

void os_data_dir(char *out, size_t n)
{
	os_exe_dir(out, n);
}

int os_can_pick(void)
{
	return 1;
}

void os_exe_dir(char *out, size_t n)
{
	wchar_t p[WPATH];
	const DWORD l = GetModuleFileNameW(NULL, p, WPATH);
	wchar_t *slash = (l && l < WPATH) ? wcsrchr(p, L'\\') : NULL;
	if (!slash) { snprintf(out, n, "."); return; }
	*slash = 0;
	narrow(p, out, n);
}

static int std_handle_ok(DWORD which)
{
	const HANDLE h = GetStdHandle(which);
	return h && h != INVALID_HANDLE_VALUE;
}

int os_console_attach(void)
{
	const int out = std_handle_ok(STD_OUTPUT_HANDLE), err = std_handle_ok(STD_ERROR_HANDLE);
	if (out && err) return 1;                /* a console program, or redirected */
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) return err;
	if (!out) freopen("CONOUT$", "w", stdout);
	if (!err) freopen("CONOUT$", "w", stderr);
	return 1;
}

void os_mkdir(const char *path)
{
	wchar_t wp[WPATH];
	if (widen(path, wp, WPATH)) _wmkdir(wp);
	else _mkdir(path);
}

int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n)
{
	HWND owner = NULL;
	wchar_t initial[WPATH], file[WPATH] = L"";
#ifdef CL_HAVE_SDL
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if (win && SDL_GetWindowWMInfo(win, &wm))
		owner = wm.info.win.window;
#else
	(void)win;
#endif
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	out[0] = 0;
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	if (kind == OS_PICK_BIOS)
	{
		ofn.lpstrFilter = L"Casio Loopy BIOS (*.bin, *.zip)\0*.bin;*.zip\0All files\0*.*\0";
		ofn.lpstrTitle = L"Choose the Casio Loopy BIOS";
	}
	else
	{
		ofn.lpstrFilter = L"Casio cartridges (*.bin, *.zip)\0*.bin;*.zip\0All files\0*.*\0";
		ofn.lpstrTitle = L"Open a PV-1000 or Loopy cartridge";
	}
	if (dir && *dir && widen(dir, initial, WPATH))
	{
		for (wchar_t *c = initial; *c; c++)
			if (*c == L'/') *c = L'\\';          /* the dialog wants native separators */
		ofn.lpstrInitialDir = initial;
	}
	ofn.lpstrFile = file;
	ofn.nMaxFile = WPATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	if (!GetOpenFileNameW(&ofn)) return 0;
	narrow(file, out, n);
	return out[0] != 0;
}


#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

void os_utf8_args(int *argc, char ***argv)
{
	(void)argc; (void)argv;                  /* already UTF-8 */
}

FILE *os_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

int os_fseek(FILE *f, long long off, int whence)
{
	return fseeko(f, (off_t)off, whence);    /* 64-bit off_t: _FILE_OFFSET_BITS=64 */
}

long long os_ftell(FILE *f)
{
	return (long long)ftello(f);
}

void os_exe_dir(char *out, size_t n)
{
	char p[4096];
	char *slash = NULL;
#ifdef __APPLE__
	char raw[4096];
	uint32_t size = sizeof(raw);
	if (_NSGetExecutablePath(raw, &size) == 0 && realpath(raw, p)) slash = strrchr(p, '/');
#else
	const ssize_t l = readlink("/proc/self/exe", p, sizeof(p) - 1);
	if (l > 0) { p[l] = 0; slash = strrchr(p, '/'); }
#endif
	if (!slash) { snprintf(out, n, "."); return; }
	*slash = 0;
	snprintf(out, n, "%s", p);
}

/* every folder along path */
static void mkdir_p(const char *path)
{
	char p[4096];
	snprintf(p, sizeof(p), "%s", path);
	for (char *c = p + 1; *c; c++)
		if (*c == '/')
		{
			*c = 0;
			mkdir(p, 0755);
			*c = '/';
		}
	mkdir(p, 0755);
}

/* unpacked somewhere writable (not an AppImage, not installed): keep everything together */
static int portable(char *dir, size_t n)
{
	os_exe_dir(dir, n);
	return !getenv("APPIMAGE") && access(dir, W_OK) == 0;
}

/* $XDG_<var> or ~/<fallback>, then /casiolator */
static void xdg_dir(char *out, size_t n, const char *var, const char *fallback)
{
	const char *x = getenv(var), *home = getenv("HOME");
	if (x && *x) snprintf(out, n, "%s/casiolator", x);
	else snprintf(out, n, "%s/%s/casiolator", home ? home : ".", fallback);
	mkdir_p(out);
}

void os_config_dir(char *out, size_t n)
{
	if (!portable(out, n)) xdg_dir(out, n, "XDG_CONFIG_HOME", ".config");
}

void os_data_dir(char *out, size_t n)
{
	if (!portable(out, n)) xdg_dir(out, n, "XDG_DATA_HOME", ".local/share");
}

/* from a terminal stderr is a tty; started from a file manager it goes nowhere */
int os_console_attach(void)
{
	return isatty(STDERR_FILENO);
}

void os_mkdir(const char *path)
{
	mkdir(path, 0755);
}

/* the file dialog of the desktop, through zenity (GNOME and most others) or kdialog (KDE) */
static int picker(void)
{
	static int which = -1;
	if (which < 0)
		which = system("command -v zenity >/dev/null 2>&1") == 0 ? 1
			: system("command -v kdialog >/dev/null 2>&1") == 0 ? 2 : 0;
	return which;
}

int os_can_pick(void)
{
	return picker() != 0;
}

/* s between single quotes for the shell */
static void quote(char *d, size_t n, const char *s)
{
	size_t k = 0;
	d[k++] = '\'';
	for (; *s && k + 6 < n; s++)
	{
		if (*s == '\'') { memcpy(d + k, "'\\''", 4); k += 4; }
		else d[k++] = *s;
	}
	d[k++] = '\'';
	d[k] = 0;
}

int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n)
{
	(void)win;
	const int bios = kind == OS_PICK_BIOS;
	const char *home = getenv("HOME");
	char start[2048], q[4200], cmd[4600];
	out[0] = 0;
	if (!picker()) return 0;
	snprintf(start, sizeof(start), "%s/", dir && *dir ? dir : home ? home : ".");
	quote(q, sizeof(q), start);
	if (picker() == 1)
		snprintf(cmd, sizeof(cmd), "zenity --file-selection --title=%s --filename=%s --file-filter=%s "
			"--file-filter='All files | *' 2>/dev/null",
			bios ? "'Choose the Casio Loopy BIOS'" : "'Open a PV-1000 or Loopy cartridge'", q,
			bios ? "'Loopy BIOS | *.bin *.BIN *.zip *.ZIP'" : "'Casio cartridges | *.bin *.BIN *.zip *.ZIP'");
	else
		snprintf(cmd, sizeof(cmd), "kdialog --title %s --getopenfilename %s %s 2>/dev/null",
			bios ? "'Choose the Casio Loopy BIOS'" : "'Open a PV-1000 or Loopy cartridge'", q,
			bios ? "'*.bin *.BIN *.zip *.ZIP|Loopy BIOS'" : "'*.bin *.BIN *.zip *.ZIP|Casio cartridges'");
	FILE *p = popen(cmd, "r");
	if (!p) return 0;
	if (!fgets(out, (int)n, p)) out[0] = 0;
	const int rc = pclose(p);
	out[strcspn(out, "\n")] = 0;
	return rc == 0 && out[0] != 0;
}
#endif
