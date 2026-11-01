/* Correctness regressions using the animation suite's CPU scene fixture. */
#include "../tools/monomsg/include/dwl-ipc-unstable-v2-protocol.h"

static void bug_window_state(void)
{
	Fixture f, g;
	fixture_init(&f);
	f.m.lt[0] = f.m.lt[1] = &layouts[0];
	cfg.animations.enabled = false;
	f.c.isfloating = 1;
	f.c.geom = (struct wlr_box){2100, 200, 400, 300};
	f.c.bounds = f.c.geom;
	struct wlr_box normal = f.c.geom;
	setfullscreen(&f.c, 1);
	setfullscreen(&f.c, 1);
	assert(wlr_box_equal(&normal, &f.c.prev));
	setfullscreen(&f.c, 0);
	assert(wlr_box_equal(&normal, &f.c.geom));
	setfullscreen(&f.c, 0);
	assert(wlr_box_equal(&normal, &f.c.geom));

	f.m.w = (struct wlr_box){1940, 140, 960, 660};
	movecenter(NULL);
	assert(f.c.geom.x == 1940 + (960 - 400) / 2);
	assert(f.c.geom.y == 140 + (660 - 300) / 2);
	f.m.canvas_tags = 1;
	f.m.zoom = 0.5;
	f.m.canvas_x = 123;
	f.m.canvas_y = -90;
	f.c.world_set = f.c.incanvas = 1;
	f.c.world_x = 2100;
	f.c.world_y = 200;
	f.c.world_width = 400;
	f.c.world_height = 300;
	movecenter(NULL);
	assert(abs(f.c.rendered.x + f.c.rendered.width / 2 - (1940 + 960 / 2)) <= 1);
	assert(abs(f.c.rendered.y + f.c.rendered.height / 2 - (140 + 660 / 2)) <= 1);

	fixture_init(&g);
	g.m.lt[0] = g.m.lt[1] = &layouts[0];
	g.c.isfloating = 1;
	g.c.geom.x = 3000;
	int before = g.c.geom.x;
	locked = 1;
	selmon = &f.m;
	g.output.enabled = true;
	struct wlr_output disabled_output = {.name = "DISABLED"};
	Monitor disabled = {.wlr_output = &disabled_output};
	wl_list_insert(&mons, &disabled.link);
	closemon(&f.m);
	assert(g.c.mon == &g.m && g.c.geom.x == before);
	assert(selmon == &g.m);
	wl_list_remove(&disabled.link);
	locked = 0;
	fixture_finish(&g);
	fixture_finish(&f);
	cfg.animations.enabled = true;
}

static void bug_fullscreen_monitor_move(void)
{
	Fixture f, g;
	fixture_init(&f);
	fixture_init(&g);
	f.m.lt[0] = f.m.lt[1] = g.m.lt[0] = g.m.lt[1] = &layouts[0];
	cfg.animations.enabled = false;
	f.c.isfloating = 1;
	f.c.geom = (struct wlr_box){2100, 200, 400, 300};
	f.c.bounds = f.c.geom;
	setfullscreen(&f.c, 1);
	locked = 1;
	setmon(&f.c, &g.m, 1);
	locked = 0;
	assert(f.c.prev.width == 400 && f.c.prev.height == 300);
	setfullscreen(&f.c, 0);
	assert(f.c.geom.width == 400 && f.c.geom.height == 300);
	fixture_finish(&g);
	fixture_finish(&f);
	cfg.animations.enabled = true;
}

static void bug_actions_and_status(void)
{
	Fixture f;
	fixture_init(&f);
	f.m.lt[0] = f.m.lt[1] = &layouts[0];
	Client urgent = {.mon = &f.m, .tags = 1, .isurgent = 1};
	wl_list_insert(clients.prev, &urgent.link);
	status_collect(&f.m, &f.m.status);
	assert(get_tag_status(1, &f.m) == 2);
	wl_list_remove(&urgent.link);
	status_collect(&f.m, &f.m.status);
	assert(get_tag_status(1, &f.m) == 1);
	status_collect(&f.m, &f.m.status);
	assert(get_tag_status(2, &f.m) == 0);

	cfg.input.repeat_rate = 40;
	cfg.input.repeat_delay = 250;
	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);
	const xkb_keysym_t sym = XKB_KEY_a;
	kb_group->nsyms = 1;
	kb_group->keysyms = &sym;
	cfg.nkeybinds = 1;
	CfgCommand command = {.action = "togglegaps"};
	CfgKeybind binding = {.key = sym, .lua_ref = LUA_NOREF, .command = &command};
	cfg.keybinds = &binding;
	locked = 1;
	int gaps = f.m.gaps;
	keyrepeat(kb_group);
	assert(f.m.gaps == gaps && kb_group->nsyms == 0);
	locked = 0;
	cfg.nkeybinds = 0;
	cfg.keybinds = NULL;

	wl_list_init(&pointers);
	cfg.appearance.inner_border_px = cfg.appearance.outer_border_px = 2;
	Config *fresh = malloc(sizeof(*fresh));
	assert(fresh);
	*fresh = cfg;
	fresh->log_level = WLR_DEBUG;
	on_config_reload(fresh, NULL);
	assert(wlr_log_get_verbosity() == WLR_DEBUG);
	wlr_log_init(WLR_ERROR, NULL);
	free(fresh);
	destroykeyboardgroup(&kb_group->destroy, NULL);
	kb_group = NULL;

	/* No argument toggles the two remembered layouts. */
	f.m.lt[0] = &layouts[0];
	f.m.lt[1] = &layouts[1];
	f.m.sellt = 0;
	f.c.isfloating = 1;
	dispatch_action("setlayout", NULL, 0);
	assert(f.m.lt[f.m.sellt] == &layouts[1]);
	dispatch_action("setlayout", NULL, 0);
	assert(f.m.lt[f.m.sellt] == &layouts[0]);
	fixture_finish(&f);
}

static void bug_dwindle_depth(void)
{
	Fixture f;
	fixture_init(&f);
	cfg.animations.enabled = false;
	enum { N = 600 };
	Client *extra = calloc(N, sizeof(*extra));
	assert(extra);
	DwindleNode **root = &f.m.dwindle_root[0];
	dwindle_insert(root, &f.c, NULL, NULL);
	for (int i = 0; i < N; i++) {
		extra[i].mon = &f.m;
		extra[i].tags = 1;
		extra[i].surface.xdg = &f.xdg;
		wl_list_insert(&clients, &extra[i].link);
		dwindle_insert(root, &extra[i], &f.c, NULL);
	}
	dwindle(&f.m);
	/* Pruning more than the old 512-entry stale limit must also finish. */
	for (int i = 0; i < N; i++)
		extra[i].isfloating = 1;
	dwindle(&f.m);
	assert(*root && !(*root)->is_node && (*root)->client == &f.c);
	for (int i = 0; i < N; i++)
		wl_list_remove(&extra[i].link);
	free(extra);
	dwindle_free_tree(*root);
	*root = NULL;
	fixture_finish(&f);
	cfg.animations.enabled = true;
}

static void bug_hit_testing(void)
{
	/* A scene rectangle without an owner must safely reach the root. */
	struct wlr_scene_rect *rect = wlr_scene_rect_create(layers[LyrTop], 200, 200,
			(float[4]){1, 1, 1, 1});
	xytonode(50, 50, NULL, NULL, NULL, NULL, NULL);
	wlr_scene_node_destroy(&rect->node);
	LayerSurface layer = {.type = LayerShell};
	layer.scene = wlr_scene_tree_create(layers[LyrTop]);
	layer.scene->node.data = &layer;
	wlr_scene_rect_create(layer.scene, 200, 200, (float[4]){1, 1, 1, 1});
	LayerSurface *hit = NULL;
	xytonode(50, 50, NULL, NULL, &hit, NULL, NULL);
	assert(hit == &layer);
	wlr_scene_node_destroy(&layer.scene->node);

	Fixture f, g;
	fixture_init(&f);
	fixture_init(&g);
	f.c.scene->node.data = &f.c;
	g.c.scene->node.data = &g.c;
	attach_buffer(&g, 992, 692);
	client_render(&f.c);
	client_render(&g.c);
	wlr_scene_node_reparent(&f.c.scene->node, layers[LyrFloat]);
	wlr_scene_node_set_position(&f.c.scene->node, 0, 0);
	wlr_scene_node_set_position(&g.c.scene->node, 0, -10);
	Client *client = NULL;
	struct wlr_surface *surface = NULL;
	xytonode(50, 1, &surface, &client, NULL, NULL, NULL);
	assert(client == &f.c && !surface);
	fixture_finish(&g);
	fixture_finish(&f);
}

static void bug_pointer_constraints(void)
{
	Fixture f, g;
	fixture_init(&f);
	fixture_init(&g);
	struct wlr_pointer_constraint_v1 a = {.seat = seat, .surface = f.surface};
	struct wlr_pointer_constraint_v1 b = {.seat = seat, .surface = g.surface};
	wl_list_insert(&pointer_constraints->constraints, &a.link);
	wl_list_insert(&pointer_constraints->constraints, &b.link);
	pointerfocus(NULL, f.surface, 10, 10, 0);
	assert(active_constraint == &a);
	pointerfocus(NULL, g.surface, 10, 10, 0);
	assert(active_constraint == &b);
	pointerfocus(NULL, NULL, 0, 0, 0);
	assert(!active_constraint);
	assert(constraint_activations == 2 && constraint_deactivations == 2);
	wl_list_remove(&a.link);
	wl_list_remove(&b.link);
	fixture_finish(&g);
	fixture_finish(&f);
}

static void bug_output_test(void)
{
	Fixture f;
	fixture_init(&f);
	f.output.data = &f.m;
	wl_signal_init(&f.output.events.destroy);
	f.m.asleep = 1;
	struct wlr_box before = f.m.m;
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	wlr_output_configuration_head_v1_create(config, &f.output);
	outputmgrapplyortest(config, 1);
	assert(output_tests == 1 && f.m.asleep == 1);
	assert(wlr_box_equal(&before, &f.m.m) && f.c.mon == &f.m);
	fixture_finish(&f);
}

static void bug_workspace_ids(void)
{
	Fixture f, g;
	fixture_init(&f);
	fixture_init(&g);
	f.output.name = "TEST-1";
	g.output.name = "TEST-2";
	ext_workspace_manager = wlr_ext_workspace_manager_v1_create(dpy, 1);
	f.m.ext_group = wlr_ext_workspace_group_handle_v1_create(ext_workspace_manager, 0);
	g.m.ext_group = wlr_ext_workspace_group_handle_v1_create(ext_workspace_manager, 0);
	ext_workspace_create(1, &f.m);
	ext_workspace_create(1, &g.m);
	struct ext_workspace *first = wl_container_of(ext_workspaces.next, first, link);
	struct ext_workspace *last = wl_container_of(ext_workspaces.prev, last, link);
	assert(first->handle->id && last->handle->id);
	assert(strcmp(first->handle->id, last->handle->id));
	assert(!strcmp(first->handle->name, last->handle->name));
	/* A duplicate connector name must not advertise a duplicate stable ID. */
	g.output.name = "TEST-1";
	ext_workspace_create(1, &g.m);
	struct ext_workspace *duplicate = wl_container_of(ext_workspaces.prev, duplicate, link);
	assert(!duplicate->handle->id);
	ext_workspace_destroy(duplicate);
	ext_workspace_destroy(last);
	ext_workspace_destroy(first);
	wlr_ext_workspace_group_handle_v1_destroy(g.m.ext_group);
	wlr_ext_workspace_group_handle_v1_destroy(f.m.ext_group);
	fixture_finish(&g);
	fixture_finish(&f);
}

static void performance_regressions(void)
{
	Fixture f, g;
	fixture_init(&f);
	fixture_init(&g);
	f.m.canvas_tags = 1;
	f.m.zoom = 0.6;
	f.c.incanvas = f.c.world_set = f.c.isfloating = 1;
	f.c.world_x = f.c.geom.x;
	f.c.world_y = f.c.geom.y;
	f.c.world_width = f.c.geom.width;
	f.c.world_height = f.c.geom.height;
	client_render(&f.c);
	CanvasNodeState *state = canvas_node_state(&f.buffer->node);
	assert(state);
	client_render(&f.c);
	assert(canvas_node_state(&f.buffer->node) == state);
	f.m.canvas_x = 120;
	canvas_layout(&f.m);
	assert(canvas_node_state(&f.buffer->node) == state);
	assert(f.c.scene->node.x == f.m.m.x - 72);
	/* A new native buffer/crop must invalidate the position-only path. */
	attach_buffer(&f, 992, 692);
	wl_event_loop_dispatch_idle(event_loop);
	canvas_layout(&f.m);
	assert(f.buffer->dst_width == (int)round(992 * 0.6));
	f.m.canvas_tags = 0;
	canvas_restore(&f.c);
	f.m.zoom = 1;
	f.c.pop_start = g.c.pop_start = 1000;
	client_render(&f.c);
	client_render(&g.c);
	double untouched = g.c.opacity;
	now_ms = 1050;
	assert(anim_tick(&f.m));
	assert(f.c.opacity > 0 && g.c.opacity == untouched);
	struct wlr_surface_output overlap = {.output = &g.output};
	wl_list_insert(&f.surface->current_outputs, &overlap.link);
	double before = f.c.opacity;
	now_ms = 1100;
	assert(anim_tick(&g.m));
	assert(f.c.opacity > before); /* spanning windows follow affected outputs */
	wl_list_remove(&overlap.link);
	f.output.enabled = g.output.enabled = true;
	scheduled_frames = 0;
	anim_start(&f.m);
	assert(scheduled_frames == 1 && scheduled_output == &f.output);
	f.output.enabled = g.output.enabled = false;
	fixture_finish(&g);
	fixture_finish(&f);
}

static void bug_sync_done(void *data, struct wl_callback *cb, uint32_t serial)
{
	*(bool *)data = true;
	wl_callback_destroy(cb);
}

static void bug_roundtrip(void)
{
	bool done = false;
	const struct wl_callback_listener listener = {.done = bug_sync_done};
	struct wl_callback *cb = wl_display_sync(client_display);
	wl_callback_add_listener(cb, &listener, &done);
	dispatch_requests();
	while (!done)
		assert(wl_display_dispatch(client_display) >= 0);
}

static unsigned bug_tag_count, bug_ipc_frames;
static void bug_ipc_tag(void *data, struct zdwl_ipc_output_v2 *output,
		uint32_t tag, uint32_t state, uint32_t count, uint32_t focused)
{
	if (!tag)
		bug_tag_count = count;
}
static void bug_ipc_frame(void *data, struct zdwl_ipc_output_v2 *output) { bug_ipc_frames++; }
static void bug_ipc_u32(void *data, struct zdwl_ipc_output_v2 *output, uint32_t value) {}
static void bug_ipc_string(void *data, struct zdwl_ipc_output_v2 *output, const char *value) {}
static void bug_ipc_toggle(void *data, struct zdwl_ipc_output_v2 *output) {}
static void bug_ipc_tags(void *data, struct zdwl_ipc_manager_v2 *manager, uint32_t count) {}
static void bug_ipc_layout(void *data, struct zdwl_ipc_manager_v2 *manager, const char *name) {}

static void bug_ipc(void)
{
	Fixture f;
	fixture_init(&f);
	f.m.lt[0] = f.m.lt[1] = &layouts[0];
	Client other = {.mon = &f.m, .tags = 1};
	wl_list_insert(clients.prev, &other.link);
	struct zdwl_ipc_manager_v2 *manager = (void *)wl_proxy_create(
			(struct wl_proxy *)client_display, &zdwl_ipc_manager_v2_interface);
	const struct zdwl_ipc_manager_v2_listener manager_listener = {
		.tags = bug_ipc_tags, .layout = bug_ipc_layout,
	};
	zdwl_ipc_manager_v2_add_listener(manager, &manager_listener, NULL);
	ipc_bind(test_client, NULL, 2, wl_proxy_get_id((struct wl_proxy *)manager));
	struct zdwl_ipc_output_v2 *proxy = (void *)wl_proxy_create(
			(struct wl_proxy *)manager, &zdwl_ipc_output_v2_interface);
	const struct zdwl_ipc_output_v2_listener listener = {
		.tag = bug_ipc_tag, .frame = bug_ipc_frame, .active = bug_ipc_u32,
		.layout = bug_ipc_u32, .title = bug_ipc_string, .appid = bug_ipc_string,
		.layout_symbol = bug_ipc_string, .fullscreen = bug_ipc_u32,
		.floating = bug_ipc_u32, .toggle_visibility = bug_ipc_toggle,
	};
	zdwl_ipc_output_v2_add_listener(proxy, &listener, NULL);
	IpcOutput *out = ecalloc(1, sizeof(*out));
	out->mon = &f.m;
	out->resource = wl_resource_create(test_client, &zdwl_ipc_output_v2_interface, 2,
			wl_proxy_get_id((struct wl_proxy *)proxy));
	wl_resource_set_implementation(out->resource, &ipc_output_impl, out, ipc_output_destroy);
	wl_list_insert(&ipc_outputs, &out->link);
	status_emit();
	bug_roundtrip();
	assert(bug_tag_count == 2);
	zdwl_ipc_manager_v2_release(manager);
	bug_roundtrip();
	unsigned frames_before = bug_ipc_frames;
	zdwl_ipc_output_v2_set_tags(proxy, 0, 1);
	bug_roundtrip();
	assert(f.m.seltags == 0 && f.m.tagset[0] == 1 && bug_ipc_frames == frames_before);
	zdwl_ipc_output_v2_set_layout(proxy, 0);
	bug_roundtrip();
	assert(bug_ipc_frames > frames_before);
	frames_before = bug_ipc_frames;
	printstatus();
	printstatus();
	printstatus();
	wl_event_loop_dispatch_idle(event_loop);
	bug_roundtrip();
	assert(bug_ipc_frames == frames_before + 1);
	ipc_cleanupmon(&f.m);
	assert(!out->mon);
	zdwl_ipc_output_v2_set_tags(proxy, 2, 1);
	zdwl_ipc_output_v2_release(proxy);
	bug_roundtrip();
	assert(wl_list_empty(&ipc_outputs));
	wl_list_remove(&other.link);
	fixture_finish(&f);
}

static void bug_regressions(void)
{
	int saved_stdout = dup(STDOUT_FILENO);
	FILE *status = tmpfile();
	assert(saved_stdout >= 0 && status);
	fflush(stdout);
	assert(dup2(fileno(status), STDOUT_FILENO) >= 0);
	bug_window_state();
	bug_fullscreen_monitor_move();
	bug_actions_and_status();
	bug_dwindle_depth();
	bug_hit_testing();
	bug_pointer_constraints();
	bug_output_test();
	bug_workspace_ids();
	performance_regressions();
	bug_ipc();
	fflush(stdout);
	assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	close(saved_stdout);
	fclose(status);
	puts("window-manager bug regressions passed");
}
