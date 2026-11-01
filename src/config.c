/* Casiolator - settings file (see config.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "os.h"

config g_config = { .volume = 100, .scale = 3 };

static void config_path(char *out, size_t n)
{
	char dir[1024];
	os_config_dir(dir, sizeof(dir));
	snprintf(out, n, "%s/casiolator.ini", dir);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = 0;
	return s;
}

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

int config_load(void)
{
	char path[1100], line[1200];
	config_path(path, sizeof(path));
	FILE *f = os_fopen(path, "r");
	if (!f) return 0;
	while (fgets(line, sizeof(line), f))
	{
		char *s = trim(line), *eq = strchr(s, '=');
		if (*s == '#' || *s == ';' || !eq) continue;
		*eq = 0;
		const char *key = trim(s), *val = trim(eq + 1);
		if (!strcmp(key, "game_folder")) snprintf(g_config.game_dir, sizeof(g_config.game_dir), "%s", val);
		else if (!strcmp(key, "loopy_bios")) snprintf(g_config.loopy_bios, sizeof(g_config.loopy_bios), "%s", val);
		else if (!strcmp(key, "loopy_sound")) snprintf(g_config.loopy_sound, sizeof(g_config.loopy_sound), "%s", val);
		else if (!strcmp(key, "volume")) g_config.volume = clamp(atoi(val), 0, 100);
		else if (!strcmp(key, "scale")) g_config.scale = clamp(atoi(val), 1, 8);
	}
	fclose(f);
	return 1;
}

int config_save(void)
{
	char path[1100];
	config_path(path, sizeof(path));
	FILE *f = os_fopen(path, "w");
	if (!f) return 0;
	fprintf(f, "# Casiolator settings, rewritten on exit\n");
	fprintf(f, "game_folder = %s\n", g_config.game_dir);
	fprintf(f, "loopy_bios = %s\n", g_config.loopy_bios);
	fprintf(f, "loopy_sound = %s\n", g_config.loopy_sound);
	fprintf(f, "volume = %d\n", g_config.volume);
	fprintf(f, "scale = %d\n", g_config.scale);
	return fclose(f) == 0;
}
