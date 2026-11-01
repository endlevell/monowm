#define _XOPEN_SOURCE 700
/* Here be a small dragon.
 * Not as large as the monster
 * in src/mono.c
 * but you get the idea */

#include "mono/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-core.h>

/* Lua */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/* wlroots */
#include <wlr/util/log.h>
#include <wlr/types/wlr_keyboard.h>
#include <libinput.h>

/* Forward declarations from src/mono.c  */
extern struct wl_list mons;
extern struct wl_list clients;
extern void dispatch_action(const char *action, const char (*args)[CFG_MAX_STRLEN], int nargs);

void arrange(void *m);
lua_State *g_L = NULL;
static Config *g_loading_cfg = NULL;

/* Internal watcher state */

typedef struct {
	int                  inotify_fd;
	int                  dir_wd;  /* config's directory: saves, renames, recreation */
	int                  target_wd; /* symlink target directory, possibly dir_wd */
	char                 target_name[NAME_MAX + 1];
	char                 path[1024];
	const char          *name;    /* basename within path */
	config_reload_cb     cb;
	void                *userdata;
	struct wl_event_source *event_src;
} WatchState;

/* Helpers */

static void
lua_log_error(lua_State *L, const char *context)
{
	fprintf(stderr, "mono config [%s]: %s\n",
	        context, lua_tostring(L, -1));
	lua_pop(L, 1);
}

/* Get a string field from the table at the top of the stack
 * Writes into buf and returns false and leaves buf untouched on failure
 */

static bool
lua_get_string(lua_State *L, const char *key, char *buf, size_t bufsz)
{
	lua_getfield(L, -1, key);
	bool ok = lua_isstring(L, -1);
	if (ok)
		strncpy(buf, lua_tostring(L, -1), bufsz - 1);
	lua_pop(L, 1);
	return ok;
}

/* Get an integer field. It falls back to `def` if missing or wrong type.
 * Fractional values round instead of becoming 0 as lua_tointeger would. */

static int
lua_get_int(lua_State *L, const char *key, int def)
{
	lua_getfield(L, -1, key);
	int val = def;
	if (lua_isnumber(L, -1)) {
		lua_Number n = round(lua_tonumber(L, -1));
		if (n >= INT_MIN && n <= INT_MAX)
			val = (int)n;
		else
			fprintf(stderr, "mono config: '%s' out of range, using %d\n",
			        key, def);
	}
	lua_pop(L, 1);
	return val;
}

/* Sizes such as border widths and gaps; negative values use `def` */

static unsigned int
lua_get_size(lua_State *L, const char *key, unsigned int def)
{
	int val = lua_get_int(L, key, (int)def);
	if (val < 0) {
		fprintf(stderr, "mono config: '%s' must not be negative, using %u\n",
		        key, def);
		return def;
	}
	return (unsigned int)val;
}

static double
lua_get_double(lua_State *L, const char *key, double def)
{
	lua_getfield(L, -1, key);
	double val = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
	lua_pop(L, 1);
	return val;
}

static bool
lua_get_bool(lua_State *L, const char *key, bool def)
{
	lua_getfield(L, -1, key);
	bool val = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : def;
	lua_pop(L, 1);
	return val;
}

static void
parse_color_hex(uint32_t hex, float out[4])
{
	out[3] = ( hex        & 0xFF) / 255.0f;
	out[0] = ((hex >> 24) & 0xFF) / 255.0f * out[3];
	out[1] = ((hex >> 16) & 0xFF) / 255.0f * out[3];
	out[2] = ((hex >>  8) & 0xFF) / 255.0f * out[3];
}

/* Accepts 0xRRGGBBAA numbers and "#RRGGBB" / "#RRGGBBAA" strings */

static void
lua_get_color(lua_State *L, const char *key, float out[4],
              float r, float g, float b, float a)
{
	const char *str;
	size_t len;

	out[0] = r; out[1] = g; out[2] = b; out[3] = a;
	lua_getfield(L, -1, key);
	if (lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0
			&& lua_tointeger(L, -1) <= 0xFFFFFFFF) {
		parse_color_hex((uint32_t)lua_tointeger(L, -1), out);
	} else if (lua_type(L, -1) == LUA_TSTRING
			&& (str = lua_tostring(L, -1))[0] == '#'
			&& ((len = strlen(str + 1)) == 6 || len == 8)
			&& strspn(str + 1, "0123456789abcdefABCDEF") == len) {
		uint32_t v = (uint32_t)strtoul(str + 1, NULL, 16);
		parse_color_hex(len == 6 ? v << 8 | 0xFF : v, out);
	} else if (!lua_isnil(L, -1)) {
		fprintf(stderr, "mono config: bad color for '%s', expected "
		        "0xRRGGBBAA or \"#RRGGBB[AA]\", using default\n", key);
	}
	lua_pop(L, 1);
}

static uint32_t
parse_modifier(const char *name)
{
	if (!strcmp(name, "shift")) return WLR_MODIFIER_SHIFT;
	if (!strcmp(name, "ctrl"))  return WLR_MODIFIER_CTRL;
	if (!strcmp(name, "alt"))   return WLR_MODIFIER_ALT;
	if (!strcmp(name, "logo"))  return WLR_MODIFIER_LOGO;
	if (!strcmp(name, "mod2"))  return WLR_MODIFIER_MOD2;
	if (!strcmp(name, "mod3"))  return WLR_MODIFIER_MOD3;
	if (!strcmp(name, "mod5"))  return WLR_MODIFIER_MOD5;
	return 0;
}

/* Read the optional "mods" string array of the table at the stack top.
 * Returns 0 on an unknown modifier, since dropping it would widen the bind. */
static int
lua_get_mods(lua_State *L, uint32_t *mods)
{
	int ok = 1;

	*mods = 0;
	lua_getfield(L, -1, "mods");
	if (lua_istable(L, -1)) {
		int nm = (int)lua_rawlen(L, -1);
		for (int m = 1; m <= nm && ok; m++) {
			lua_rawgeti(L, -1, m);
			if (lua_isstring(L, -1)) {
				const char *name = lua_tostring(L, -1);
				uint32_t mod = parse_modifier(name);
				if (!mod) {
					fprintf(stderr, "mono config: unknown modifier '%s', skipping bind\n",
					        name);
					ok = 0;
				}
				*mods |= mod;
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	return ok;
}

/* Section parsers */

static void
parse_appearance(lua_State *L, CfgAppearance *a)
{
	lua_getglobal(L, "appearance");
	if (!lua_istable(L, -1)) {
		fprintf(stderr, "mono config: 'appearance' table missing' \n");
		lua_pop(L, 1);
		/* defaults */
		a->inner_border_px = 1;
		a->outer_border_px = 1;
		a->gaps       = 10;
		a->smart_gaps = false;
		parse_color_hex(0x222222ff, a->root_color);
		parse_color_hex(0x00000000, a->inner_border_color);
		parse_color_hex(0x00000000, a->outer_border_color);
		parse_color_hex(0xe8e8e8ff, a->inner_focus_color);
		parse_color_hex(0xe8e8e8ff, a->outer_focus_color);
		parse_color_hex(0xff0000ff, a->inner_urgent_color);
		parse_color_hex(0xff0000ff, a->outer_urgent_color);
		a->fullscreen_bg[3] = 1.0f;
		return;
	}

	a->inner_border_px = lua_get_size(L, "inner_border_px", 1);
	a->outer_border_px = lua_get_size(L, "outer_border_px", 1);
	a->gaps       = lua_get_size(L, "gaps",       10);
	a->smart_gaps = lua_get_bool(L, "smart_gaps", false);

	lua_get_color(L, "root_color",          a->root_color,          0.133f,0.133f,0.133f,1.f);
	lua_get_color(L, "inner_border_color",  a->inner_border_color,  0.f,0.f,0.f,0.f);
	lua_get_color(L, "outer_border_color",  a->outer_border_color,  0.f,0.f,0.f,0.f);
	lua_get_color(L, "inner_focus_color",   a->inner_focus_color,   0.91f,0.91f,0.91f,1.f);
	lua_get_color(L, "outer_focus_color",   a->outer_focus_color,   0.91f,0.91f,0.91f,1.f);
	lua_get_color(L, "inner_urgent_color",  a->inner_urgent_color,  1.f,0.f,0.f,1.f);
	lua_get_color(L, "outer_urgent_color",  a->outer_urgent_color,  1.f,0.f,0.f,1.f);

	lua_get_color(L, "fullscreen_bg",       a->fullscreen_bg,       0.f,0.f,0.f,1.f);

	lua_pop(L, 1); /* pop appearance table */
}

static void
parse_animations(lua_State *L, CfgAnimations *an)
{
	an->enabled = true;
	an->speed   = 1.0;

	lua_getglobal(L, "animations");
	if (lua_istable(L, -1)) {
		an->enabled = lua_get_bool(L, "enabled", true);
		an->speed   = lua_get_double(L, "speed", 1.0);
		if (an->speed <= 0) {
			fprintf(stderr, "mono config: animations.speed must be positive, using 1\n");
			an->speed = 1.0;
		}
	}
	lua_pop(L, 1);
}

static void
parse_input(lua_State *L, CfgInput *inp)
{
	lua_getglobal(L, "input");
	if (!lua_istable(L, -1)) {
		fprintf(stderr, "mono config: 'input' table missing, "
		        "using defaults. You'll feel the repeat delay.\n");
		lua_pop(L, 1);
		inp->repeat_rate  = 50;
		inp->repeat_delay = 150;
		inp->tap_to_click = true;
		inp->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		return;
	}

	inp->repeat_rate            = lua_get_int (L, "repeat_rate",            50);
	inp->repeat_delay           = lua_get_int (L, "repeat_delay",           150);
	inp->tap_to_click           = lua_get_bool(L, "tap_to_click",           true);
	inp->tap_and_drag           = lua_get_bool(L, "tap_and_drag",           true);
	inp->drag_lock              = lua_get_bool(L, "drag_lock",              true);
	inp->natural_scrolling      = lua_get_bool(L, "natural_scrolling",      false);
	inp->disable_while_typing   = lua_get_bool(L, "disable_while_typing",   true);
	inp->left_handed            = lua_get_bool(L, "left_handed",            false);
	inp->middle_button_emulation= lua_get_bool(L, "middle_button_emulation",false);
	inp->accel_speed            = lua_get_double(L, "accel_speed",          0.0);

	char scratch[CFG_MAX_STRLEN] = {0};

	lua_get_string(L, "scroll_method", scratch, sizeof(scratch));
	if      (!strcmp(scratch, "2fg"))    inp->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	else if (!strcmp(scratch, "edge"))   inp->scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
	else if (!strcmp(scratch, "button")) inp->scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
	else                                 inp->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

	lua_get_string(L, "accel_profile", scratch, sizeof(scratch));
	if (!strcmp(scratch, "flat"))        inp->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
	else                                 inp->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;

	lua_get_string(L, "click_method", scratch, sizeof(scratch));
	if      (!strcmp(scratch, "clickfinger"))   inp->click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
	else if (!strcmp(scratch, "button_areas"))  inp->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	else                                        inp->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

	lua_get_string(L, "send_events_mode", scratch, sizeof(scratch));
	if      (!strcmp(scratch, "disabled"))             inp->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
	else if (!strcmp(scratch, "disabled_on_external")) inp->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
	else                                               inp->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

	lua_get_string(L, "tap_button_map", scratch, sizeof(scratch));
	if (!strcmp(scratch, "lmr")) inp->tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LMR;
	else                         inp->tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

	lua_pop(L, 1);
}

static void
parse_rules(lua_State *L, Config *cfg)
{
	lua_getglobal(L, "rules");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	cfg->nrules = 0;
	int n = (int)lua_rawlen(L, -1);
	for (int i = 1; i <= n && cfg->nrules < CFG_MAX_RULES; i++) {
		lua_rawgeti(L, -1, i);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

		CfgRule *r = &cfg->rules[cfg->nrules++];
		memset(r, 0, sizeof(*r));
		lua_get_string(L, "app_id", r->app_id, sizeof(r->app_id));
		lua_get_string(L, "title",  r->title,  sizeof(r->title));
		r->tags     = (uint32_t)lua_get_int(L, "tags",     0);
		r->floating = lua_get_bool(L, "floating", false);
		r->monitor  = lua_get_int(L, "monitor", -1);

		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static void
parse_monitors(lua_State *L, Config *cfg)
{
	lua_getglobal(L, "monitors");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		cfg->nmonitors = 1;
		CfgMonitorRule *m = &cfg->monitors[0];
		memset(m, 0, sizeof(*m));
		m->mfact   = 0.55f;
		m->nmaster = 1;
		m->scale   = 1.0f;
		strcpy(m->layout, "dwindle");
		m->x = m->y = -1;
		return;
	}

	cfg->nmonitors = 0;
	int n = (int)lua_rawlen(L, -1);
	for (int i = 1; i <= n && cfg->nmonitors < CFG_MAX_MONITORS; i++) {
		lua_rawgeti(L, -1, i);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }

		CfgMonitorRule *m = &cfg->monitors[cfg->nmonitors++];
		memset(m, 0, sizeof(*m));

		lua_getfield(L, -1, "name");
		if (lua_isstring(L, -1))
			strncpy(m->name, lua_tostring(L, -1), sizeof(m->name)-1);
		lua_pop(L, 1);

		m->mfact   = (float)lua_get_double(L, "mfact",   0.55);
		m->nmaster =        lua_get_int   (L, "nmaster", 1);
		m->scale   = (float)lua_get_double(L, "scale",   1.0);
		m->x       =        lua_get_int   (L, "x",       -1);
		m->y       =        lua_get_int   (L, "y",       -1);
		m->transform =      lua_get_int   (L, "transform", 0);
		lua_get_string(L, "layout", m->layout, sizeof(m->layout));
		if (!m->layout[0]) strcpy(m->layout, "dwindle");

		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static void
command_finish(CfgCommand *command)
{
	if (command) {
		free(command->args);
		free(command);
	}
}

void
config_finish(Config *cfg)
{
	for (int i = 0; i < cfg->nkeybinds; i++)
		command_finish(cfg->keybinds[i].command);
	for (int i = 0; i < cfg->nbuttons; i++)
		command_finish(cfg->buttons[i].command);
	free(cfg->keybinds);
	free(cfg->buttons);
	memset(cfg, 0, sizeof(*cfg));
}

static CfgCommand *
parse_command(lua_State *L)
{
	CfgCommand *command = calloc(1, sizeof(*command));
	char args[CFG_MAX_ARGS][CFG_MAX_STRLEN] = {{0}};
	if (!command)
		return NULL;
	lua_get_string(L, "action", command->action, sizeof(command->action));
	lua_getfield(L, -1, "args");
	if (lua_istable(L, -1)) {
		int n = (int)lua_rawlen(L, -1);
		for (int i = 1; i <= n && command->nargs < CFG_MAX_ARGS; i++) {
			lua_rawgeti(L, -1, i);
			if (lua_isstring(L, -1))
				strncpy(args[command->nargs++], lua_tostring(L, -1), CFG_MAX_STRLEN - 1);
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	if (command->nargs) {
		command->args = calloc(command->nargs, sizeof(*command->args));
		if (!command->args) {
			free(command);
			return NULL;
		}
		memcpy(command->args, args, command->nargs * sizeof(*args));
	}
	return command;
}

static int
parse_keybinds(lua_State *L, Config *cfg)
{
	lua_getglobal(L, "keybinds");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	int n = (int)lua_rawlen(L, -1);
	for (int i = 1; i <= n && cfg->nkeybinds < CFG_MAX_KEYBINDS; i++) {
		lua_rawgeti(L, -1, i);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
		CfgKeybind kb = {.lua_ref = LUA_NOREF};
		if (!lua_get_mods(L, &kb.mods)) { lua_pop(L, 1); continue; }
		char keyname[CFG_MAX_STRLEN] = {0};
		lua_get_string(L, "key", keyname, sizeof(keyname));
		kb.key = xkb_keysym_from_name(keyname, XKB_KEYSYM_CASE_INSENSITIVE);
		if (kb.key == XKB_KEY_NoSymbol) {
			fprintf(stderr, "mono config: unknown key '%s', skipping bind\n", keyname);
			lua_pop(L, 1);
			continue;
		}
		if (!(kb.command = parse_command(L)))
			return 0;
		CfgKeybind *bindings = realloc(cfg->keybinds, (cfg->nkeybinds + 1) * sizeof(*bindings));
		if (!bindings) { command_finish(kb.command); return 0; }
		cfg->keybinds = bindings;
		cfg->keybinds[cfg->nkeybinds++] = kb;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return 1;
}

static int
parse_buttons(lua_State *L, Config *cfg)
{
	lua_getglobal(L, "buttons");
	if (!lua_istable(L, -1)) { lua_pop(L, 1); return 1; }
	int n = (int)lua_rawlen(L, -1);
	for (int i = 1; i <= n && cfg->nbuttons < CFG_MAX_KEYBINDS; i++) {
		lua_rawgeti(L, -1, i);
		if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
		CfgButton button = {0};
		if (!lua_get_mods(L, &button.mods)) { lua_pop(L, 1); continue; }
		char name[CFG_MAX_STRLEN] = {0};
		lua_get_string(L, "button", name, sizeof(name));
		if (!strcmp(name, "left")) button.button = BTN_LEFT;
		else if (!strcmp(name, "right")) button.button = BTN_RIGHT;
		else if (!strcmp(name, "middle")) button.button = BTN_MIDDLE;
		else button.button = (uint32_t)atoi(name);
		if (!(button.command = parse_command(L)))
			return 0;
		CfgButton *buttons = realloc(cfg->buttons, (cfg->nbuttons + 1) * sizeof(*buttons));
		if (!buttons) { command_finish(button.command); return 0; }
		cfg->buttons = buttons;
		cfg->buttons[cfg->nbuttons++] = button;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return 1;
}


static void
parse_autostart(lua_State *L, Config *cfg)
{
	lua_getglobal(L, "autostart");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	cfg->nautostart = 0;
	int n = (int)lua_rawlen(L, -1);
	for (int i = 1; i <= n && cfg->nautostart < CFG_MAX_AUTOSTART; i++) {
		lua_rawgeti(L, -1, i);
		if (!lua_isstring(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		strncpy(cfg->autostart[cfg->nautostart].cmd,
		        lua_tostring(L, -1),
		        CFG_MAX_AUTOSTART_CMD - 1);
		cfg->nautostart++;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

/* Public API */

char *
config_get_path(char *buf, size_t bufsz)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, bufsz, "%s/mono/config.lua", xdg);
	else {
		const char *home = getenv("HOME");
		if (!home) home = "/root"; /* you're probably root, aren't you */
		snprintf(buf, bufsz, "%s/.config/mono/config.lua", home);
	}
	return buf;
}

static int
l_mono_bind(lua_State *L)
{
	if (!g_loading_cfg)
		return 0;
	Config *cfg = g_loading_cfg;
	if (cfg->nkeybinds >= CFG_MAX_KEYBINDS) {
		fprintf(stderr, "mono config: too many keybinds, skipping mono.bind\n");
		return 0;
	}

	const char *combo = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (!combo[0] || combo[0] == '+' || combo[strlen(combo) - 1] == '+'
			|| strstr(combo, "++"))
		return luaL_error(L, "invalid binding combo '%s'", combo);

	CfgKeybind binding = {.lua_ref = LUA_NOREF};
	CfgKeybind *kb = &binding;

	/* combo is "logo+shift+a" style — split on '+', last token is the key */
	char buf[CFG_MAX_STRLEN];
	strncpy(buf, combo, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *saveptr = NULL;
	char *tok = strtok_r(buf, "+", &saveptr);
	char *last = tok;
	if (!last)
		return luaL_error(L, "binding combo must contain a key");
	kb->mods = 0;
	while (tok) {
		char *next = strtok_r(NULL, "+", &saveptr);
		if (next) {
			uint32_t mod = parse_modifier(tok);
			if (!mod)
				return luaL_error(L, "unknown modifier '%s'", tok);
			kb->mods |= mod;
			last = next;
		}
		tok = next;
	}

	kb->key = xkb_keysym_from_name(last, XKB_KEYSYM_CASE_INSENSITIVE);
	if (kb->key == XKB_KEY_NoSymbol) {
		fprintf(stderr, "mono config: unknown key '%s' in mono.bind, skipping\n", last);
		return 0;
	}

	CfgKeybind *bindings = realloc(cfg->keybinds, (cfg->nkeybinds + 1) * sizeof(*bindings));
	if (!bindings)
		return luaL_error(L, "out of memory allocating binding");
	cfg->keybinds = bindings;
	lua_pushvalue(L, 2); /* duplicate the function onto the stack top */
	kb->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	cfg->keybinds[cfg->nkeybinds++] = binding;
	return 0;
}

static int
l_mono_action(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char args[CFG_MAX_ARGS][CFG_MAX_STRLEN];
	int nargs = 0;
	int n = lua_gettop(L) - 1;

	for (int i = 0; i < n && nargs < CFG_MAX_ARGS; i++) {
		const char *s = lua_tostring(L, i + 2);
		if (s) {
			strncpy(args[nargs], s, CFG_MAX_STRLEN - 1);
			args[nargs][CFG_MAX_STRLEN - 1] = '\0';
			nargs++;
		}
	}

	dispatch_action(name, args, nargs);
	return 0;
}

static int
l_mono_config(lua_State *L)
{
	static const char *const sections[] = {
		"sloppy_focus", "bypass_surface_visibility", "log_level",
		"appearance", "animations", "input", "rules", "monitors", "keybinds", "buttons", "autostart",
	};

	luaL_checktype(L, 1, LUA_TTABLE);
	for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
		lua_getfield(L, 1, sections[i]);
		if (!lua_isnil(L, -1))
			lua_setglobal(L, sections[i]);
		else
			lua_pop(L, 1);
	}
	return 0;
}

static int
l_mono_append(lua_State *L)
{
	const char *section = lua_tostring(L, lua_upvalueindex(1));
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getglobal(L, section);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
	}
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushvalue(L, 1);
	lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
	lua_setglobal(L, section);
	return 0;
}

static int
l_mono_autostart(lua_State *L)
{
	luaL_checkstring(L, 1);
	lua_getglobal(L, "autostart");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
	}
	lua_pushvalue(L, 1);
	lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
	lua_setglobal(L, "autostart");
	return 0;
}

static int
l_mono_action_callback(lua_State *L)
{
	const char *action = lua_tostring(L, lua_upvalueindex(1));
	char args[CFG_MAX_ARGS][CFG_MAX_STRLEN] = {{0}};
	int nargs = (int)lua_rawlen(L, lua_upvalueindex(2));

	for (int i = 0; i < nargs; i++) {
		lua_rawgeti(L, lua_upvalueindex(2), i + 1);
		strncpy(args[i], lua_tostring(L, -1), CFG_MAX_STRLEN - 1);
		lua_pop(L, 1);
	}
	dispatch_action(action, args, nargs);
	return 0;
}

static int
l_mono_action_factory(lua_State *L)
{
	const char *action = lua_tostring(L, lua_upvalueindex(1));
	int nargs = lua_gettop(L);
	if (nargs > CFG_MAX_ARGS)
		return luaL_error(L, "too many action arguments (maximum %d)", CFG_MAX_ARGS);

	lua_pushstring(L, action);
	lua_createtable(L, nargs, 0);
	for (int i = 1; i <= nargs; i++) {
		if (!lua_isstring(L, i) && !lua_isnumber(L, i))
			return luaL_argerror(L, i, "string or number expected");
		luaL_tolstring(L, i, NULL);
		lua_rawseti(L, -2, i);
	}
	lua_pushcclosure(L, l_mono_action_callback, 2);
	return 1;
}

static const luaL_Reg mono_lib[] = {
	{"bind", l_mono_bind},
	{"action", l_mono_action},
	{"config", l_mono_config},
	{"autostart", l_mono_autostart},
	{NULL, NULL}
};

static void
register_mono_builder(lua_State *L, const char *name, const char *section)
{
	lua_pushstring(L, section);
	lua_pushcclosure(L, l_mono_append, 1);
	lua_setfield(L, -2, name);
}

static void
register_mono_action(lua_State *L, const char *name, const char *action)
{
	lua_pushstring(L, action);
	lua_pushcclosure(L, l_mono_action_factory, 1);
	lua_setfield(L, -2, name);
}

static void
register_mono_api(lua_State *L)
{
	luaL_newlib(L, mono_lib);
	register_mono_builder(L, "rule", "rules");
	register_mono_builder(L, "monitor", "monitors");
	register_mono_builder(L, "button", "buttons");

	register_mono_action(L, "spawn", "spawn");
	register_mono_action(L, "kill_client", "killclient");
	register_mono_action(L, "toggle_floating", "togglefloating");
	register_mono_action(L, "toggle_fullscreen", "togglefullscreen");
	register_mono_action(L, "toggle_gaps", "togglegaps");
	register_mono_action(L, "quit", "quit");
	register_mono_action(L, "view", "view");
	register_mono_action(L, "toggle_view", "toggleview");
	register_mono_action(L, "tag", "tag");
	register_mono_action(L, "toggle_tag", "toggletag");
	register_mono_action(L, "view_shift", "viewshift");
	register_mono_action(L, "tag_shift", "tagshift");
	register_mono_action(L, "focus", "focusdir");
	register_mono_action(L, "swap", "swapdir");
	register_mono_action(L, "focus_monitor", "focusmon");
	register_mono_action(L, "move_to_monitor", "tagmon");
	register_mono_action(L, "layout", "setlayout");
	register_mono_action(L, "move_resize", "moveresize");
	register_mono_action(L, "toggle_canvas", "togglecanvas");
	register_mono_action(L, "canvas_pan", "canvaspan");
	register_mono_action(L, "move_center", "movecenter");
	register_mono_action(L, "vt", "chvt");
	lua_setglobal(L, "mono");
}

int
config_load(const char *path, Config *out)
{
	Config fresh = {0};
	Config *cfg = &fresh;
	lua_State *old_L = g_L;
	lua_State *L;

	memset(cfg, 0, sizeof(*cfg));

	L = luaL_newstate();
	if (!L) {
		fprintf(stderr, "mono config: failed to create Lua state. "
		        "Truly rock bottom.\n");
		return -1;
	}
	luaL_openlibs(L);
	register_mono_api(L);
	g_L = L;

	g_loading_cfg = cfg;
	if (luaL_dofile(L, path) != LUA_OK) {
		lua_log_error(L, "load");
		goto failed;
	}
	g_loading_cfg = NULL;

	lua_getglobal(L, "sloppy_focus");
	cfg->sloppyfocus = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : true;
	lua_pop(L, 1);
	lua_getglobal(L, "bypass_surface_visibility");
	cfg->bypass_surface_visibility = lua_isboolean(L, -1)
	                                 ? lua_toboolean(L, -1) : false;
	lua_pop(L, 1);
	lua_getglobal(L, "log_level");
	if (lua_isstring(L, -1)) {
		const char *ll = lua_tostring(L, -1);
		if      (!strcmp(ll, "silent")) cfg->log_level = WLR_SILENT;
		else if (!strcmp(ll, "info"))   cfg->log_level = WLR_INFO;
		else if (!strcmp(ll, "debug"))  cfg->log_level = WLR_DEBUG;
		else                            cfg->log_level = WLR_ERROR;
	} else {
		cfg->log_level = WLR_ERROR;
	}
	lua_pop(L, 1);
	parse_appearance(L, &cfg->appearance);
	parse_animations(L, &cfg->animations);
	parse_input     (L, &cfg->input);
	parse_rules     (L, cfg);
	parse_monitors  (L, cfg);
	if (!parse_keybinds(L, cfg) || !parse_buttons(L, cfg)) {
		fprintf(stderr, "mono config: out of memory parsing bindings\n");
		goto failed;
	}
	parse_autostart (L, cfg);
	if (old_L)
		lua_close(old_L);
	config_finish(out);
	*out = fresh;
	return 0;
failed:
	g_loading_cfg = NULL;
	config_finish(cfg);
	lua_close(L);
	g_L = old_L;
	return -1;
}

/* Apply functions */

void
config_apply_appearance(const Config *cfg)
{
	(void)cfg;
}

void
config_apply_input(const Config *cfg)
{
	(void)cfg;
}

void
config_apply_keybinds(const Config *cfg)
{
	(void)cfg;
}

/* Process groups of autostart runs (one per startup or reload that added
 * commands) */
typedef struct AutostartGroup {
	pid_t pid;
	struct AutostartGroup *next;
} AutostartGroup;
static AutostartGroup *autostart_groups;

static void
autostart_prune(void)
{
	AutostartGroup **link = &autostart_groups;
	while (*link) {
		AutostartGroup *group = *link;
		/* Retain a live sequencer until it has been reaped. */
		if (kill(-group->pid, 0) < 0 && errno == ESRCH
				&& getpgid(group->pid) < 0 && errno == ESRCH) {
			*link = group->next;
			free(group);
		} else {
			link = &group->next;
		}
	}
}

/* Autostarted programs belong to this session. Left running, they outlive
 * it, e.g. an audio server that keeps the next session from starting its
 * own. Programs that detach into their own session are not affected. */
void
config_autostart_stop(void)
{
	while (autostart_groups) {
		AutostartGroup *group = autostart_groups;
		kill(-group->pid, SIGTERM);
		autostart_groups = group->next;
		free(group);
	}
}

void
config_autostart_run(const CfgAutostart *cmds, int n)
{
	if (n == 0)
		return;
	autostart_prune();
	AutostartGroup *group = malloc(sizeof(*group));
	if (!group) {
		perror("mono autostart: malloc");
		return;
	}
	int ready[2];
	if (pipe(ready) < 0) {
		perror("mono autostart: pipe");
		free(group);
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("mono autostart: fork");
		close(ready[0]);
		close(ready[1]);
		free(group);
		return;
	}
	if (pid != 0) {
		/* Returning only after setsid makes immediate shutdown reliable. */
		char started;
		close(ready[1]);
		while (read(ready[0], &started, 1) < 0 && errno == EINTR)
			;
		close(ready[0]);
		/* The sequencer's setsid() makes pid the process group of
		 * everything it starts; remember it for config_autostart_stop() */
		group->pid = pid;
		group->next = autostart_groups;
		autostart_groups = group;
		return; /* compositor continues (sequencer runs in child) */
	}

	/* The sequencer child resets any inherited signal handlers and becomes
	 * a new session so the compositor's SIGCHLD handling doesn't
	 * interfere with our waitpid calls. */
	signal(SIGCHLD, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	free(group);
	close(ready[0]);
	if (setsid() < 0)
		_exit(1);
	if (write(ready[1], "1", 1) != 1)
		_exit(1);
	close(ready[1]);
	/* stdout may be the -s status pipe; keep command output out of it,
	 * as spawn() does */
	dup2(STDERR_FILENO, STDOUT_FILENO);

	for (int i = 0; i < n; i++) {
		const char *cmd = cmds[i].cmd;

		pid_t cpid = fork();
		if (cpid < 0) {
			fprintf(stderr, "mono autostart: fork for '%s': %s\n",
			        cmd, strerror(errno));
			continue;
		}
		if (cpid == 0) {
			execl("/bin/sh", "sh", "-c", cmd, NULL);
			fprintf(stderr, "mono autostart: exec '%s': %s\n",
			        cmd, strerror(errno));
			_exit(1);
		}

		/* wait half a second because one shotters will fly past, but
		 * daemon processes (some, like swaybg) won't because of
		 * daemon black magic. To say the least, it's good for the soul */
		struct timespec deadline, now;
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		deadline.tv_nsec += 500 * 1000 * 1000;
		if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000 * 1000 * 1000;
		}

		int status;
		int done = 0;
		while (!done) {
			pid_t r = waitpid(cpid, &status, WNOHANG);
			if (r == cpid) {
				done = 1;
				if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
					fprintf(stderr, "mono autostart: '%s' exited with status %d\n",
					        cmd, WEXITSTATUS(status));
			} else {
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (now.tv_sec > deadline.tv_sec ||
				    (now.tv_sec == deadline.tv_sec &&
				     now.tv_nsec >= deadline.tv_nsec))
					break; /* looks like a daemon, move on */
				struct timespec sl = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
				nanosleep(&sl, NULL);
			}
		}
	}

	_exit(0);
}

/* inotify watcher */

static int
path_is_link(const char *path)
{
	struct stat st;
	return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

/* Watch the target's directory so atomic replacement doesn't orphan the
 * watch on the previous inode. Keep the old directory while a target is
 * temporarily missing, so recreating it can recover hot reload. */
static void
watch_target(WatchState *ws)
{
	int wd = -1;
	if (path_is_link(ws->path)) {
		char *target = realpath(ws->path, NULL);
		if (!target)
			return;
		char *slash = strrchr(target, '/');
		strncpy(ws->target_name, slash + 1, sizeof(ws->target_name) - 1);
		if (slash == target)
			slash[1] = '\0';
		else
			*slash = '\0';
		wd = inotify_add_watch(ws->inotify_fd, target,
				IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MASK_ADD);
		free(target);
	}
	if (ws->target_wd >= 0 && ws->target_wd != wd && ws->target_wd != ws->dir_wd)
		inotify_rm_watch(ws->inotify_fd, ws->target_wd);
	ws->target_wd = wd;
}

static int
watch_dispatch(int fd, uint32_t mask, void *data)
{
	WatchState *ws = data;
	int changed = 0, replaced = 0;

	/* Drain the inotify events */
	_Alignas(struct inotify_event) char buf[4096];
	ssize_t len = read(fd, buf, sizeof(buf));
	if (len < 0) {
		if (errno != EAGAIN)
			perror("mono config: inotify read");
		return 0;
	}

	/*
	 * Watching the directory rather than the file keeps working when
	 * editors save by delete-and-recreate or rename, which would orphan a
	 * watch on the file's inode.
	 */
	for (char *p = buf; p < buf + len; ) {
		const struct inotify_event *ev = (const struct inotify_event *)p;
		p += sizeof(*ev) + ev->len;

		if (ev->mask & IN_IGNORED) {
			if (ev->wd == ws->target_wd)
				ws->target_wd = -1;
			if (ev->wd == ws->dir_wd) {
				fprintf(stderr, "mono config: config directory removed, hot reload stopped\n");
				ws->dir_wd = -1;
			}
			continue;
		}
		int config_entry = ev->wd == ws->dir_wd && ev->len && !strcmp(ev->name, ws->name);
		int target_entry = ev->wd == ws->target_wd && ev->len && !strcmp(ev->name, ws->target_name);
		if (config_entry || target_entry) {
			/* Wait for a new regular file to finish being written. */
			if ((ev->mask & IN_CREATE) && !(config_entry && path_is_link(ws->path)))
				continue;
			changed = replaced = 1;
		}
	}

	if (replaced)
		watch_target(ws);
	if (!changed)
		return 0;

	/* Re-parse */
	Config *fresh = calloc(1, sizeof(Config));
	if (!fresh) return 0;

	if (config_load(ws->path, fresh) == 0) {
		fprintf(stderr, "mono config: reloaded '%s'\n", ws->path);
		if (ws->cb)
			ws->cb(fresh, ws->userdata);
	} else {
		fprintf(stderr, "mono config: reload failed, keeping old config. "
		        "Story of my life.\n");
	}

	config_finish(fresh);
	free(fresh);
	return 0;
}

struct wl_event_source *
config_watch_start(struct wl_event_loop *loop, const char *path,
                   config_reload_cb cb, void *userdata)
{
	WatchState *ws = calloc(1, sizeof(*ws));
	if (!ws) return NULL;

	ws->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ws->inotify_fd < 0) {
		perror("mono config: inotify_init1");
		free(ws);
		return NULL;
	}

	strncpy(ws->path, path, sizeof(ws->path) - 1);
	ws->target_wd = -1;

	char dir[sizeof(ws->path)];
	const char *slash = strrchr(ws->path, '/');
	if (slash) {
		size_t n = slash == ws->path ? 1 : (size_t)(slash - ws->path);
		memcpy(dir, ws->path, n);
		dir[n] = '\0';
		ws->name = slash + 1;
	} else {
		strcpy(dir, ".");
		ws->name = ws->path;
	}

	ws->dir_wd = inotify_add_watch(ws->inotify_fd, dir,
	                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
	if (ws->dir_wd < 0) {
		perror("mono config: inotify_add_watch");
		close(ws->inotify_fd);
		free(ws);
		return NULL;
	}
	watch_target(ws);
	ws->cb       = cb;
	ws->userdata = userdata;

	struct wl_event_source *src = wl_event_loop_add_fd(
		loop, ws->inotify_fd,
		WL_EVENT_READABLE,
		watch_dispatch, ws
	);

	if (!src) {
		fprintf(stderr, "mono config: failed to add inotify to event loop. "
		        "Classic.\n");
		close(ws->inotify_fd);
		free(ws);
		return NULL;
	}

	ws->event_src = src;
	fprintf(stderr, "mono config: watching '%s'\n", path);
	return (struct wl_event_source *)ws;
}

void
config_watch_stop(struct wl_event_source *src)
{
	if (!src) return;
	WatchState *ws = (WatchState *)src;
	wl_event_source_remove(ws->event_src);
	close(ws->inotify_fd);
	free(ws);
}
