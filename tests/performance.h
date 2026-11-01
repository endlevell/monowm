/* CPU scene microbenchmark; shared-library allocations and GPU work are excluded. */
static int perf_tracking;
static size_t perf_allocs, perf_frees;
void *__real_calloc(size_t n, size_t size);
void __real_free(void *ptr);
void *__wrap_calloc(size_t n, size_t size)
{
	if (perf_tracking) perf_allocs++;
	return __real_calloc(n, size);
}
void __wrap_free(void *ptr)
{
	if (perf_tracking && ptr) perf_frees++;
	__real_free(ptr);
}

static void performance(void)
{
	Config sample = {0};
	assert(!config_load("examples/config.lua", &sample));
	size_t bytes = sizeof(sample) + sample.nkeybinds * sizeof(CfgKeybind)
			+ sample.nbuttons * sizeof(CfgButton);
	for (int i = 0; i < sample.nkeybinds + sample.nbuttons; i++) {
		CfgCommand *command = i < sample.nkeybinds ? sample.keybinds[i].command
				: sample.buttons[i - sample.nkeybinds].command;
		if (command) bytes += sizeof(*command) + command->nargs * sizeof(*command->args);
	}
	printf("config_struct_bytes=%zu example_config_bytes=%zu keys=%d buttons=%d\n",
			sizeof(Config), bytes, sample.nkeybinds, sample.nbuttons);
	config_finish(&sample);
	for (int extra = 0; extra <= 100; extra += 100) {
		Fixture f;
		fixture_init(&f);
		f.m.canvas_tags = 1;
		f.m.zoom = 0.6;
		f.c.world_set = f.c.incanvas = f.c.isfloating = 1;
		f.c.world_x = f.c.geom.x;
		f.c.world_y = f.c.geom.y;
		f.c.world_width = f.c.geom.width;
		f.c.world_height = f.c.geom.height;
		for (int i = 0; i < extra; i++)
			wlr_scene_rect_create(f.c.scene_surface, 10, 10, (float[4]){1, 1, 1, 1});
		client_render(&f.c);
		for (int mode = 0; mode < 3; mode++) {
			perf_allocs = perf_frees = 0;
			struct timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a);
			perf_tracking = 1;
			for (int i = 0; i < 1000; i++) {
				if (mode == 1) {
					f.m.canvas_x = i % 200;
					canvas_layout(&f.m);
				} else {
					if (mode == 2) f.m.zoom = 0.6 + (i % 20) * 0.005;
					client_render(&f.c);
				}
			}
			perf_tracking = 0;
			clock_gettime(CLOCK_MONOTONIC, &b);
			double ms = (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
			printf("extra_nodes=%d mode=%s updates=1000 allocations=%zu frees=%zu cpu_scene_ms=%.3f\n",
					extra, mode == 1 ? "pan" : mode == 2 ? "zoom" : "steady", perf_allocs, perf_frees, ms);
			assert(!perf_allocs && !perf_frees);
		}
		fixture_finish(&f);
	}
	lua_close(g_L);
	g_L = NULL;
}
