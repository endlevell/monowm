#define _XOPEN_SOURCE 700
#include <assert.h>
#include "../src/parser.c"

static char dispatched_action[CFG_MAX_STRLEN];
void dispatch_action(const char *action, const char (*args)[CFG_MAX_STRLEN], int nargs)
{
	strncpy(dispatched_action, action, sizeof(dispatched_action) - 1);
}

static void binding_regression(void)
{
	Config config = {0};
	assert(!config_load("examples/config.lua", &config));
	assert(config.nkeybinds == 62 && config.nbuttons == 4);
	for (int i = 0; i < config.nkeybinds; i++)
		assert(!config.keybinds[i].command && config.keybinds[i].lua_ref != LUA_NOREF);
	char path[] = "/tmp/mono-bindings-test-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	FILE *file = fdopen(fd, "w");
	assert(file);
	fputs("mono.bind('logo+x', function() end); error('intentional rollback')", file);
	fclose(file);
	CfgKeybind *old = config.keybinds;
	lua_State *old_L = g_L;
	assert(config_load(path, &config) < 0);
	assert(config.keybinds == old && config.nkeybinds == 62 && g_L == old_L);
	lua_rawgeti(g_L, LUA_REGISTRYINDEX, config.keybinds[0].lua_ref);
	assert(lua_pcall(g_L, 0, 0, 0) == LUA_OK);
	assert(!strcmp(dispatched_action, "spawn"));
	file = fopen(path, "w");
	assert(file);
	fputs("appearance={};input={};keybinds={{mods={'logo'},key='a',action='spawn',"
		"args={'sh','-c','echo test'}}};buttons={{mods={},button='left',action='togglecanvas'}}", file);
	fclose(file);
	assert(!config_load(path, &config));
	assert(config.nkeybinds == 1 && config.nbuttons == 1);
	assert(config.keybinds[0].command->nargs == 3);
	assert(!strcmp(config.keybinds[0].command->args[2], "echo test"));
	assert(!config.buttons[0].command->args && !config.buttons[0].command->nargs);
	file = fopen(path, "w");
	assert(file);
	fputs("appearance={};input={};for i=1,257 do mono.bind('logo+a',function() end) end", file);
	fclose(file);
	assert(!config_load(path, &config));
	assert(config.nkeybinds == CFG_MAX_KEYBINDS);
	config_finish(&config);
	lua_close(g_L);
	g_L = NULL;
	unlink(path);
}

static void write_config(const char *path, unsigned gaps)
{
	FILE *f = fopen(path, "w");
	assert(f);
	fprintf(f, "appearance = { gaps = %u, root_color = '#FF804080' }\n", gaps);
	fputs("mono.bind('logo+a', mono.toggle_gaps())\n", f);
	assert(!fclose(f));
}

static unsigned reloads, loaded_gaps;
static Config watched_config;
static void reloaded(Config *config, void *data)
{
	config_finish(&watched_config);
	watched_config = *config;
	memset(config, 0, sizeof(*config));
	config = &watched_config;
	reloads++;
	loaded_gaps = config->appearance.gaps;
	assert(fabs(config->appearance.root_color[0] - 128.0 / 255) < 1e-6);
	assert(fabs(config->appearance.root_color[1] - 128.0 / 255 * 128 / 255) < 1e-6);
}

static void drain(struct wl_event_loop *loop)
{
	for (int i = 0; i < 4; i++)
		assert(wl_event_loop_dispatch(loop, 0) >= 0);
}

static void watch_regression(void)
{
	char root[] = "/tmp/mono-parser-test-XXXXXX";
	assert(mkdtemp(root));
	char config[PATH_MAX], dir[PATH_MAX], target[PATH_MAX], tmp[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s/target", root);
	assert(!mkdir(dir, 0700));
	snprintf(config, sizeof(config), "%s/config.lua", root);
	snprintf(target, sizeof(target), "%s/target/config.lua", root);
	snprintf(tmp, sizeof(tmp), "%s/target/new.lua", root);
	write_config(target, 1);
	assert(!symlink("target/config.lua", config));
	struct wl_event_loop *loop = wl_event_loop_create();
	struct wl_event_source *watch = config_watch_start(loop, config, reloaded, NULL);
	assert(watch);
	write_config(target, 2);
	drain(loop);
	assert(reloads == 1 && loaded_gaps == 2);
	write_config(tmp, 3);
	assert(!rename(tmp, target));
	drain(loop);
	assert(reloads == 2 && loaded_gaps == 3);
	write_config(target, 4);
	drain(loop);
	assert(reloads == 3 && loaded_gaps == 4);
	/* A failed reload keeps the previous Lua state usable. */
	lua_State *old = g_L;
	FILE *f = fopen(target, "w");
	assert(f);
	fputs("this is invalid Lua", f);
	fclose(f);
	drain(loop);
	assert(reloads == 3 && g_L == old);
	write_config(tmp, 5);
	assert(!rename(tmp, target));
	drain(loop);
	assert(reloads == 4 && loaded_gaps == 5);
	write_config(tmp, 6);
	assert(!unlink(config));
	assert(!symlink("target/new.lua", config));
	drain(loop);
	assert(reloads == 5 && loaded_gaps == 6);
	write_config(target, 7);
	drain(loop);
	assert(reloads == 5);
	write_config(tmp, 8);
	drain(loop);
	assert(reloads == 6 && loaded_gaps == 8);
	lua_rawgeti(g_L, LUA_REGISTRYINDEX, watched_config.keybinds[0].lua_ref);
	assert(lua_pcall(g_L, 0, 0, 0) == LUA_OK);
	assert(!strcmp(dispatched_action, "togglegaps"));
	config_watch_stop(watch);
	wl_event_loop_destroy(loop);
	config_finish(&watched_config);
	lua_close(g_L);
	g_L = NULL;
	unlink(config);
	unlink(target);
	unlink(tmp);
	rmdir(dir);
	rmdir(root);
}

static void autostart_regression(void)
{
	CfgAutostart command = {.cmd = "sleep 30"};
	pid_t groups[40];
	for (size_t i = 0; i < 40; i++) {
		config_autostart_run(&command, 1);
		assert(autostart_groups);
		groups[i] = autostart_groups->pid;
	}
	/* Wait for every sequencer to enter its session and finish launching. */
	for (int tries = 0; tries < 200; tries++) {
		int pending = 0;
		for (size_t i = 0; i < 40; i++) {
			if (waitpid(groups[i], NULL, WNOHANG) == 0)
				pending++;
		}
		if (!pending)
			break;
		nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
		assert(tries < 199);
	}
	size_t tracked = 0;
	for (AutostartGroup *g = autostart_groups; g; g = g->next)
		tracked++;
	assert(tracked == 40);
	config_autostart_stop();
	assert(!autostart_groups);
	/* Finished, reaped batches are pruned on the next launch. */
	CfgAutostart quick = {.cmd = "true"};
	config_autostart_run(&quick, 1);
	pid_t pid = autostart_groups->pid;
	assert(waitpid(pid, NULL, 0) == pid);
	autostart_prune();
	assert(!autostart_groups);
	/* The process group is ready even if shutdown follows immediately. */
	config_autostart_run(&command, 1);
	pid = autostart_groups->pid;
	config_autostart_stop();
	assert(waitpid(pid, NULL, 0) == pid);
}

int main(void)
{
	binding_regression();
	watch_regression();
	autostart_regression();
	puts("parser regressions passed");
	return 0;
}
