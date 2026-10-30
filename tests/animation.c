/* Exercise the compositor's scene code with CPU buffers and no desktop session. */
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <wayland-client.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/pixman.h>

static uint32_t now_ms = 1000;
static int frames;

static int test_clock_gettime(clockid_t id, struct timespec *ts)
{
	ts->tv_sec = now_ms / 1000;
	ts->tv_nsec = (now_ms % 1000) * 1000000;
	return 0;
}

static bool test_output_commit(struct wlr_scene_output *output,
		const struct wlr_scene_output_state_options *options)
{
	frames++;
	return true;
}

static void test_frame_done(struct wlr_scene_output *output, struct timespec *ts) {}

/* A running client, irrespective of this test process's parent/child status. */
static int test_waitid(idtype_t type, id_t id, siginfo_t *info, int options)
{
	memset(info, 0, sizeof(*info));
	return 0;
}

#define clock_gettime test_clock_gettime
#define wlr_scene_output_commit test_output_commit
#define wlr_scene_output_send_frame_done test_frame_done
#define waitid test_waitid
#define main mono_main
#ifndef MONO_SOURCE
#define MONO_SOURCE "../src/main.c"
#endif
/* Renaming main removes C's implicit return rule at its usage-error exit. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include MONO_SOURCE
#pragma GCC diagnostic pop
#undef main
#undef waitid
#undef wlr_scene_output_send_frame_done
#undef wlr_scene_output_commit
#undef clock_gettime

static struct wl_client *test_client;
static struct wl_display *client_display;
static struct wl_compositor *client_compositor;
static struct wl_shm *client_shm;
static struct wlr_surface *created_surface;

static void dispatch_requests(void)
{
	assert(wl_display_flush(client_display) >= 0);
	assert(wl_event_loop_dispatch(event_loop, 0) >= 0);
	wl_display_flush_clients(dpy);
}

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, "wl_compositor"))
		client_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 6);
	else if (!strcmp(interface, "wl_shm"))
		client_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener_test = {
	.global = registry_global, .global_remove = registry_remove,
};

static void new_surface_test(struct wl_listener *listener, void *data)
{
	created_surface = data;
}

static void buffer_destroy(struct wlr_buffer *buffer) {}
static const struct wlr_buffer_impl buffer_impl = {.destroy = buffer_destroy};
static const struct wl_interface toplevel_interface = {.name = "xdg_toplevel", .version = 6};

typedef struct {
	Client c;
	Monitor m;
	struct wlr_output output;
	struct wlr_surface *surface;
	struct wl_surface *client_surface;
	struct wlr_xdg_surface xdg;
	struct wlr_xdg_toplevel toplevel;
	struct wlr_xdg_client xdg_client;
	struct wlr_surface_output surface_output;
	struct wlr_buffer backing;
	struct wlr_scene_buffer *buffer;
} Fixture;

static void fixture_init(Fixture *f)
{
	memset(f, 0, sizeof(*f));
	now_ms = 1000;
	Client *c = &f->c;
	created_surface = NULL;
	f->client_surface = wl_compositor_create_surface(client_compositor);
	dispatch_requests();
	f->surface = created_surface;
	assert(f->surface);
	f->m.m = f->m.w = (struct wlr_box){1920, 100, 1000, 700};
	f->m.zoom = 1;
	for (int i = 0; i < 4; i++)
		wl_list_init(&f->m.layers[i]);
	f->m.tagset[0] = 1;
	f->m.wlr_output = &f->output;
	f->output.name = "TEST";
	wl_list_insert(&mons, &f->m.link);
	selmon = &f->m;
	c->mon = &f->m;
	c->tags = 1;
	c->geom = f->m.m;
	c->ibw = c->obw = 2;
	c->bw = 4;
	c->placed = 1;
	c->opacity = c->visual_scale = 1;
	c->slide_opacity_from = 1;
	c->surface.xdg = &f->xdg;
	f->xdg.surface = f->surface;
	f->xdg.toplevel = &f->toplevel;
	f->xdg.client = &f->xdg_client;
	f->xdg_client.client = test_client;
	f->xdg.geometry = (struct wlr_box){0, 0, 992, 692};
	f->toplevel.resource = wl_resource_create(test_client, &toplevel_interface, 6, 0);
	f->toplevel.current.width = 992;
	f->toplevel.current.height = 692;
	c->bounds = c->geom;
	f->surface->mapped = true;
	f->surface_output.output = &f->output;
	wl_list_insert(&f->surface->current_outputs, &f->surface_output.link);
	c->scene = wlr_scene_tree_create(layers[LyrTile]);
	LISTEN(&f->surface->events.commit, &c->commit, commitnotify);
	c->scene_surface = wlr_scene_subsurface_tree_create(c->scene, f->surface);
	wl_list_init(&c->canvas_nodes);
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);
	for (int i = 0; i < 4; i++) {
		c->inner_border[i] = wlr_scene_rect_create(c->scene, 0, 0, (float[4]){1, 1, 1, 1});
		c->outer_border[i] = wlr_scene_rect_create(c->scene, 0, 0, (float[4]){1, 1, 1, 1});
	}
	wlr_buffer_init(&f->backing, &buffer_impl, 992, 692);
	f->buffer = wlr_scene_buffer_create(c->scene_surface, &f->backing);
	wlr_scene_buffer_set_dest_size(f->buffer, 992, 692);
}

static void fixture_finish(Fixture *f)
{
	canvas_clear(&f->c);
	wl_list_remove(&f->c.commit.link);
	wlr_scene_node_destroy(&f->c.scene->node);
	wlr_buffer_drop(&f->backing);
	wl_list_remove(&f->surface_output.link);
	wl_surface_destroy(f->client_surface);
	dispatch_requests();
	wl_resource_destroy(f->toplevel.resource);
	wl_list_remove(&f->m.link);
	wl_list_remove(&f->c.link);
	wl_list_remove(&f->c.flink);
	selmon = NULL;
	grabc = NULL;
	cursor_mode = CurNormal;
}

static void assert_confined(struct wlr_scene_tree *tree, struct wlr_box bounds)
{
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct wlr_box box;
		if (!wlr_scene_node_coords(node, &box.x, &box.y))
			continue;
		if (node->type == WLR_SCENE_NODE_TREE) {
			assert_confined(wlr_scene_tree_from_node(node), bounds);
			continue;
		} else if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_buffer *b = wlr_scene_buffer_from_node(node);
			box.width = b->dst_width;
			box.height = b->dst_height;
		} else {
			struct wlr_scene_rect *r = wlr_scene_rect_from_node(node);
			box.width = r->width;
			box.height = r->height;
		}
		if (!wlr_box_empty(&box))
			assert(wlr_box_contains_box(&bounds, &box));
	}
}

static void workspace(void)
{
	Fixture f;
	fixture_init(&f);
	Client *c = &f.c;
	/* Include a popup extending beyond its parent's frame. */
	struct wlr_scene_tree *popup = wlr_scene_tree_create(c->scene_surface);
	wlr_scene_node_set_position(&popup->node, 950, -50);
	wlr_scene_buffer_create(popup, &f.backing);
	for (int dir = -1; dir <= 1; dir += 2) {
		c->slide_start = 1000;
		c->slide_from = 0;
		c->slide_to = dir * f.m.m.width;
		now_ms = 1100;
		client_render(c);
		assert_confined(c->scene, f.m.m);
		/* Native subsurface unmapping must survive restoring a transform. */
		wlr_scene_node_set_enabled(&popup->node, false);
		client_render(c);
		assert(!popup->node.enabled);
		wlr_scene_node_set_enabled(&popup->node, true);
		/* Reversal starts at the currently displayed offset. */
		int before = c->scene->node.x;
		anim_view(&f.m, 2, 1);
		client_render(c);
		assert(c->scene->node.x == before);
		assert_confined(c->scene, f.m.m);
	}
	/* Ending or disabling animations restores the native crop and node state. */
	cfg.animations.enabled = false;
	client_render(c);
	assert(f.buffer->node.enabled);
	assert(wlr_fbox_empty(&f.buffer->src_box));
	assert(f.buffer->dst_width == 992);
	assert(popup->node.x == 950);
	cfg.animations.enabled = true;
	/* Client viewports and every buffer rotation/flip keep their source crop
	 * intact when slides finish; the temporary crop stays inside that source. */
	const struct wlr_fbox source = {16.5, 12.25, 800, 600};
	for (int transform = 0; transform < 8; transform++) {
		canvas_restore(c);
		wlr_scene_buffer_set_transform(f.buffer, transform);
		wlr_scene_buffer_set_source_box(f.buffer, &source);
		c->slide_start = 1000;
		c->slide_from = 0;
		c->slide_to = -f.m.m.width;
		now_ms = 1100;
		client_render(c);
		assert_confined(c->scene, f.m.m);
		struct wlr_fbox cropped = f.buffer->src_box;
		assert(cropped.x >= source.x - 1e-6 && cropped.y >= source.y - 1e-6);
		assert(cropped.x + cropped.width <= source.x + source.width + 1e-6);
		assert(cropped.y + cropped.height <= source.y + source.height + 1e-6);
		assert(cropped.width * cropped.height < source.width * source.height);
		now_ms = 1701;
		client_render(c);
		assert(wlr_fbox_equal(&f.buffer->src_box, &source));
	}
	fixture_finish(&f);
}

static void opening(void)
{
	Fixture f;
	fixture_init(&f);
	f.c.pop_start = 1000;
	f.c.resize = 123; /* The client hasn't acknowledged its initial tile size. */
	frames = 0;
	rendermon(&f.m.frame, NULL);
	assert(frames == 1);
	assert(f.buffer->dst_width == (int)round(992 * ANIM_OPEN_SCALE));
	assert(f.buffer->opacity < 0.01);
	now_ms = 1050;
	rendermon(&f.m.frame, NULL);
	assert(frames == 2);
	assert(f.buffer->dst_width > (int)round(992 * ANIM_OPEN_SCALE));
	assert(f.buffer->dst_width < 992);
	assert(f.buffer->opacity > 0 && f.buffer->opacity < 1);
	now_ms = 1301;
	rendermon(&f.m.frame, NULL);
	assert(f.buffer->dst_width == 992);
	assert(f.buffer->opacity == 1);
	fixture_finish(&f);
}

static void closing(void)
{
	cfg.animations.speed = 0.65;
	for (int zoomed = 0; zoomed <= 1; zoomed++) {
		Fixture f;
		fixture_init(&f);
		f.m.canvas_tags = zoomed ? 1 : 0;
		f.m.zoom = zoomed ? 0.5 : 1;
		f.c.world_x = f.c.geom.x;
		f.c.world_y = f.c.geom.y;
		client_render(&f.c);
		int width = f.buffer->dst_width;
		wlr_scene_buffer_set_opacity(f.buffer, 0.75);
		anim_close(&f.c);
		assert(!wl_list_empty(&snapshots));
		Snapshot *s = wl_container_of(snapshots.next, s, link);
		SnapshotPart *part;
		struct wlr_scene_buffer *copy = NULL;
		wl_array_for_each(part, &s->parts)
			if (part->node->type == WLR_SCENE_NODE_BUFFER)
				copy = wlr_scene_buffer_from_node(part->node);
		assert(copy);
		/* Copy the currently displayed size and opacity before the next frame,
		 * including when a zoomed or partially faded window closes. */
		assert((copy->dst_width ? copy->dst_width : copy->buffer->width) == width);
		assert(copy->opacity == 0.75);
		double sx = 1, sy = 1;
		assert(!copy->point_accepts_input(copy, &sx, &sy));
		fixture_finish(&f); /* The snapshot must outlive the original window. */
		float previous_opacity = 0.75;
		int previous_width = width;
		for (uint32_t elapsed = 0; elapsed <= 450; elapsed += 25) {
			assert(!snapshot_render(s, 1000 + elapsed));
			assert(copy->opacity <= previous_opacity);
			assert(copy->dst_width <= previous_width);
			/* Close traverses the same easing and duration as open. */
			double opening_progress = anim_progress(ANIM_OPEN, s->start, 1000 + elapsed);
			assert(fabs(copy->opacity - 0.75 * (1.0 - opening_progress)) < 1e-6);
			previous_opacity = copy->opacity;
			previous_width = copy->dst_width;
		}
		assert(!snapshot_render(s, 1461));
		assert(anim_progress(ANIM_OPEN, s->start, 1461) < 1);
		assert(snapshot_render(s, 1462)); /* 300 ms / 0.65, like opening. */
		assert(anim_progress(ANIM_OPEN, s->start, 1462) == 1);
		snapshot_destroy(s);
		assert(wl_list_empty(&snapshots));
	}
	cfg.animations.speed = 1;
}

static void transfer_arrival(int direction, int source_canvas, int target_canvas, uint32_t elapsed)
{
	Fixture f;
	fixture_init(&f);
	Client *c = &f.c;
	uint32_t source = direction < 0 ? 4 : 1;
	f.m.tagset[0] = c->tags = source;
	f.m.canvas_tags = (source_canvas ? source : 0) | (target_canvas ? 2 : 0);
	f.m.zoom = source_canvas || target_canvas ? 0.5 : 1;
	c->geom = c->bounds = (struct wlr_box){f.m.m.x + 240, f.m.m.y + 70, 640, 400};
	f.xdg.geometry = (struct wlr_box){0, 0, 632, 392};
	f.toplevel.current.width = f.backing.width = 632;
	f.toplevel.current.height = f.backing.height = 392;
	wlr_scene_buffer_set_dest_size(f.buffer, 632, 392);
	c->isfloating = c->incanvas = c->world_set = source_canvas;
	c->world_x = f.m.m.x + (c->geom.x - f.m.m.x) / f.m.zoom;
	c->world_y = f.m.m.y + (c->geom.y - f.m.m.y) / f.m.zoom;
	c->world_width = 640;
	c->world_height = 400;
	client_render(c);
	char args[CFG_MAX_ARGS][CFG_MAX_STRLEN] = {{0}};
	strcpy(args[0], direction < 0 ? "-1" : "1");
	dispatch_action("tagshift", args, 1);
	assert(!wl_list_empty(&snapshots));
	Snapshot *s = wl_container_of(snapshots.next, s, link);
	assert(s->client == c);
	now_ms = 1000 + elapsed;
	assert(!snapshot_render(s, now_ms));
	struct wlr_box displayed = s->frame;
	displayed.x = s->tree->node.x;
	displayed.y = s->tree->node.y;
	double displayed_scale = s->scale;
	wlr_scene_node_set_enabled(&c->scene->node, false);
	/* Run the binding, then the layout work normally performed by arrange();
	 * the fixture deliberately has no output backend. */
	strcpy(args[0], "2");
	locked = 1;
	dispatch_action("view", args, 1);
	locked = 0;
	wlr_scene_node_set_enabled(&c->scene->node, true);
	if (target_canvas) {
		canvas_layout(&f.m);
	} else {
		struct wlr_box target = {f.m.m.x + 40, f.m.m.y + 30, 920, 640};
		c->bounds = target;
		f.toplevel.current.width = 912;
		f.toplevel.current.height = 632;
		resize(c, target, 0);
	}
	anim_transfer_arrivals(&f.m);
	assert(wl_list_empty(&snapshots));
	assert(c->scene->node.x == displayed.x && c->scene->node.y == displayed.y);
	assert(abs(c->rendered.width - displayed.width) <= 1);
	assert(abs(c->rendered.height - displayed.height) <= 1);
	assert(fabs(c->visual_scale - displayed_scale) < 1e-6);
	assert(c->slide_start && c->slide_to == 0 && !c->leaving);
	assert_confined(c->scene, f.m.m);
	now_ms += 50;
	anim_tick();
	assert_confined(c->scene, f.m.m);
	assert(c->tags == 2 && c->mon == &f.m);
	now_ms += 1200;
	anim_tick();
	assert(!c->slide_start && !c->geom_start && !c->leaving);
	assert(c->scene->node.enabled && c->opacity == 1);
	if (target_canvas)
		assert(c->scene->node.x == f.m.m.x
				+ (int)round((c->world_x - f.m.m.x - f.m.canvas_x) * f.m.zoom));
	else
		assert(c->scene->node.x == c->geom.x && c->scene->node.y == c->geom.y);
	fixture_finish(&f);
}

static void sending(void)
{
	/* Exercise the real Lua action dispatch used by the user's bindings. */
	FILE *status = tmpfile();
	assert(status);
	fflush(stdout);
	int saved_stdout = dup(STDOUT_FILENO);
	assert(saved_stdout >= 0);
	assert(dup2(fileno(status), STDOUT_FILENO) >= 0);
	cfg.animations.speed = 0.65;
	for (int zoomed = 0; zoomed <= 1; zoomed++) {
		for (int route = 0; route < 4; route++) {
			Fixture f;
			fixture_init(&f);
			Client *c = &f.c;
			f.m.tagset[0] = c->tags = route == 1 ? 4 : 1;
			f.m.canvas_tags = zoomed ? c->tags : 0;
			f.m.zoom = zoomed ? 0.5 : 1;
			c->isfloating = c->incanvas = c->world_set = zoomed;
			c->world_x = c->geom.x;
			c->world_y = c->geom.y;
			client_render(c);
			int width = f.buffer->dst_width;
			struct wlr_box before = c->geom;
			int before_x = c->scene->node.x;
			char args[CFG_MAX_ARGS][CFG_MAX_STRLEN] = {{0}};
			if (route < 2) {
				strcpy(args[0], route == 0 ? "1" : "-1");
				dispatch_action("tagshift", args, 1);
			} else if (route == 2) {
				strcpy(args[0], "2");
				dispatch_action("tag", args, 1);
			} else {
				IpcOutput output = {.mon = &f.m};
				struct wl_resource *resource = wl_resource_create(test_client,
						&toplevel_interface, 6, 0);
				wl_resource_set_user_data(resource, &output);
				ipc_output_handle_set_client_tags(test_client, resource, 0, 2);
				wl_resource_destroy(resource);
			}
			assert(c->tags == 2);
			assert(f.m.tagset[0] == (route == 1 ? 4u : 1u));
			assert(!VISIBLEON(c, &f.m) && !focustop(&f.m));
			assert(!wl_list_empty(&snapshots));
			Snapshot *s = wl_container_of(snapshots.next, s, link);
			assert(s->animation == ANIM_WORKSPACE);
			assert((s->slide_to > 0) == (route != 1));
			assert(s->tree->node.x == before_x);
			assert_confined(s->tree, f.m.m);
			struct wlr_scene_buffer *copy = NULL;
			SnapshotPart *part;
			wl_array_for_each(part, &s->parts) {
				if (part->node->type == WLR_SCENE_NODE_BUFFER)
					copy = wlr_scene_buffer_from_node(part->node);
				else if (part->node->type == WLR_SCENE_NODE_RECT)
					assert(!wlr_scene_rect_from_node(part->node)->accepts_input);
			}
			assert(copy && copy->dst_width == width);
			double sx = 1, sy = 1;
			assert(!copy->point_accepts_input(copy, &sx, &sy));
			/* Logical retagging and later geometry changes cannot disturb the
			 * departing picture, including when canvas_leave restored native size. */
			c->geom = (struct wlr_box){0, 0, 40, 40};
			struct wlr_scene_tree *remaining = wlr_scene_tree_create(c->scene->node.parent);
			wlr_scene_node_raise_to_top(&remaining->node);
			assert(!snapshot_render(s, 1100));
			assert(s->tree->node.link.next == &s->tree->node.parent->children);
			assert((s->tree->node.x > before_x) == (route != 1));
			assert_confined(s->tree, f.m.m);
			assert(!snapshot_render(s, 1701)); /* Shared 700 ms / 0.65 duration. */
			assert(snapshot_render(s, 2077));
			/* Switching to the destination must remove the departing picture. */
			f.m.tagset[0] = 2;
			assert(snapshot_render(s, 1100));
			f.m.tagset[0] = 8;
			assert(snapshot_render(s, 1100));
			c->geom = before;
			anim_cleanupmon(&f.m);
			assert(wl_list_empty(&snapshots));
			wlr_scene_node_destroy(&remaining->node);
			fixture_finish(&f);
		}
	}
	/* Early and late destination switches keep the currently displayed image
	 * continuous, in either direction and across tiled/canvas layouts. */
	for (int direction = -1; direction <= 1; direction += 2)
		for (int source_canvas = 0; source_canvas <= 1; source_canvas++)
			for (int target_canvas = 0; target_canvas <= 1; target_canvas++) {
				transfer_arrival(direction, source_canvas, target_canvas, 50);
				transfer_arrival(direction, source_canvas, target_canvas, 900);
			}
	/* Keep the existing tag invariants: no empty mask, and no departure when
	 * the window still belongs to a currently displayed tag. */
	Fixture f;
	fixture_init(&f);
	settags(&f.c, 0);
	settags(&f.c, 1u << TAGCOUNT);
	assert(f.c.tags == 1 && wl_list_empty(&snapshots));
	seat->keyboard_state.focused_surface = f.surface;
	settags(&f.c, 3);
	assert(f.c.tags == 3 && wl_list_empty(&snapshots));
	seat->keyboard_state.focused_surface = NULL;
	cfg.animations.enabled = false;
	settags(&f.c, 2);
	assert(f.c.tags == 2 && wl_list_empty(&snapshots));
	cfg.animations.enabled = true;
	f.c.tags = 1;
	client_render(&f.c);
	settags(&f.c, 2);
	assert(!wl_list_empty(&snapshots));
	anim_cancel_transfer(&f.c); /* The unmap path must release its owned picture. */
	assert(wl_list_empty(&snapshots));
	cfg.animations.speed = 1;
	fixture_finish(&f);
	fflush(stdout);
	assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	close(saved_stdout);
	fclose(status);
}

static void resizing(void)
{
	Fixture f;
	fixture_init(&f);
	Client *c = &f.c;
	cursor_mode = CurResize;
	grabc = c;
	struct wlr_box target = {1920, 100, 1400, 900};
	/* Configure setters are already satisfied; keep the old buffer to model
	 * a redraw that hasn't caught up with the requested frame size. */
	c->bounds = target;
	f.toplevel.current.width = target.width - 8;
	f.toplevel.current.height = target.height - 8;
	resize(c, target, 1);
	assert(c->geom_start);
	now_ms = 1050;
	client_render(c);
	struct wlr_box box = client_displayed_geom(c, now_ms);
	assert(box.width > 1000 && box.width < target.width);
	assert(f.buffer->dst_width == box.width - 8);
	assert(f.buffer->dst_height == box.height - 8);
	/* Input is mapped back from the stretched image to native surface space. */
	double sx = f.buffer->dst_width / 2.0, sy = f.buffer->dst_height / 2.0;
	assert(f.buffer->point_accepts_input(f.buffer, &sx, &sy));
	assert(fabs(sx - 496) < 0.5 && fabs(sy - 346) < 0.5);
	/* The same displayed dimensions survive an app's redraw at the new size. */
	canvas_restore(c);
	f.xdg.geometry.width = f.backing.width = target.width - 8;
	f.xdg.geometry.height = f.backing.height = target.height - 8;
	wlr_scene_buffer_set_dest_size(f.buffer, f.backing.width, f.backing.height);
	client_render(c);
	assert(f.buffer->dst_width == box.width - 8);
	assert(f.buffer->dst_height == box.height - 8);
	/* Canvas zoom composes with the animated resize. */
	f.m.canvas_tags = 1;
	f.m.zoom = 0.5;
	c->world_x = target.x;
	c->world_y = target.y;
	client_render(c);
	assert(f.buffer->dst_width == (int)round((box.width - 8) * 0.5));
	now_ms = 1301;
	f.m.canvas_tags = 0;
	client_render(c);
	assert(f.buffer->dst_width == target.width - 8);
	assert(wl_list_empty(&c->canvas_nodes));
	/* A pointer resize also starts easing on a canvas tag. Pointer moves
	 * continue following directly, and disabling animations snaps resizes. */
	f.m.canvas_tags = 1;
	target.width = 1600;
	c->bounds = target;
	f.toplevel.current.width = target.width - 8;
	resize(c, target, 1);
	assert(c->geom_start);
	cursor_mode = CurMove;
	target.x += 20;
	resize(c, target, 1);
	assert(!c->geom_start);
	cursor_mode = CurResize;
	cfg.animations.enabled = false;
	target.width += 100;
	c->bounds = target;
	f.toplevel.current.width = target.width - 8;
	resize(c, target, 1);
	assert(!c->geom_start);
	cfg.animations.enabled = true;
	fixture_finish(&f);
}

static void attach_buffer(Fixture *f, int width, int height)
{
	FILE *file = tmpfile();
	assert(file);
	int size = width * height * 4;
	assert(ftruncate(fileno(file), size) == 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(client_shm, fileno(file), size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
			width * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(f->client_surface, buffer, 0, 0);
	wl_surface_damage_buffer(f->client_surface, 0, 0, width, height);
	wl_surface_commit(f->client_surface);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	dispatch_requests();
	fclose(file);
}

static void find_surface_buffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	if (wlr_scene_surface_try_from_buffer(buffer))
		*(struct wlr_scene_buffer **)data = buffer;
}

static void redraw_at_zoom(double zoom)
{
	Fixture f;
	fixture_init(&f);
	attach_buffer(&f, 992, 692);
	struct wlr_scene_buffer *buffer = NULL;
	wlr_scene_node_for_each_buffer(&f.c.scene_surface->node, find_surface_buffer, &buffer);
	assert(buffer && buffer->buffer);
	f.m.canvas_tags = zoom < 1 ? 1 : 0;
	f.m.zoom = zoom;
	f.c.world_x = f.c.geom.x;
	f.c.world_y = f.c.geom.y;
	client_render(&f.c);
	assert(buffer->dst_width == (int)round(992 * zoom));
	assert(buffer->dst_height == (int)round(692 * zoom));
	/* Real SHM commits exercise wlroots' native geometry reset and our idle
	 * refresh, including a buffer whose size changes during an animation. */
	f.c.geom_from = f.c.geom;
	f.c.geom.width = 1400;
	f.c.geom.height = 900;
	f.c.geom_start = 1000;
	now_ms = 1050;
	f.xdg.geometry.width = 1392;
	f.xdg.geometry.height = 892;
	f.c.bounds = f.c.geom;
	f.toplevel.current.width = 1392;
	f.toplevel.current.height = 892;
	attach_buffer(&f, 1392, 892);
	wl_event_loop_dispatch_idle(event_loop);
	struct wlr_box box = client_displayed_geom(&f.c, now_ms);
	assert(buffer->dst_width == (int)round((box.width - 8) * zoom));
	assert(buffer->dst_height == (int)round((box.height - 8) * zoom));
	for (int i = 0; i < 3; i++) {
		wl_surface_commit(f.client_surface);
		dispatch_requests();
		wl_event_loop_dispatch_idle(event_loop);
		assert(buffer->dst_width == (int)round((box.width - 8) * zoom));
	}
	fixture_finish(&f);
}

static void redraw(void)
{
	redraw_at_zoom(1);
	redraw_at_zoom(0.5);
}

static void canvas_toggle(void)
{
	Fixture f;
	fixture_init(&f);
	client_render(&f.c);
	struct wlr_box tiled = f.c.geom;
	/* The fixture has no output backend; perform the canvas placement that
	 * arrange() normally does before the first zoom frame. */
	dispatch_action("togglecanvas", NULL, 0);
	canvas_layout(&f.m);
	anim_backdrop(&f.m, 1);
	assert(f.m.canvas_tags == 1 && f.c.incanvas && f.c.isfloating);
	assert(f.m.zoom == 1 && f.m.zoom_start && f.m.zoom_to == CANVAS_OVERVIEW_ZOOM);
	assert(f.buffer->dst_width == 992);
	now_ms = 1050;
	anim_tick();
	assert(f.m.zoom > CANVAS_OVERVIEW_ZOOM && f.m.zoom < 1);
	assert(f.buffer->dst_width > (int)round(992 * CANVAS_OVERVIEW_ZOOM));
	assert(f.buffer->dst_width < 992);
	assert(f.m.backdrop_mix > 0 && f.m.backdrop_mix < 1);
	now_ms = 1301;
	anim_tick();
	assert(f.m.zoom == CANVAS_OVERVIEW_ZOOM && !f.m.zoom_start);
	assert(f.buffer->dst_width == (int)round(992 * CANVAS_OVERVIEW_ZOOM));
	/* The overview zoom must not leave a synthetic pan to recenter afterward. */
	assert(f.m.backdrop_offset_x == 0 && f.m.backdrop_offset_y == 0);
	assert(!anim_tick());
	struct wlr_box overview = f.c.rendered;
	int content_width = f.buffer->dst_width;
	dispatch_action("togglecanvas", NULL, 0);
	resize(&f.c, tiled, 0); /* Dwindle's destination during arrange(). */
	anim_backdrop(&f.m, 0);
	assert(!f.m.canvas_tags && !f.c.incanvas && !f.c.isfloating);
	assert(f.c.geom_start && !f.m.zoom_start);
	assert(abs(f.c.rendered.width - overview.width) <= 1);
	assert(abs(f.buffer->dst_width - content_width) <= 1);
	assert(f.c.scene->node.x == overview.x && f.c.scene->node.y == overview.y);
	now_ms = 1351;
	anim_tick();
	assert(f.c.rendered.width > overview.width && f.c.rendered.width < tiled.width);
	assert(f.xdg.geometry.width == 992); /* Applications keep native dimensions. */
	now_ms = 1602;
	anim_tick();
	assert(wlr_box_equal(&f.c.rendered, &tiled));
	assert(f.m.backdrop_mix == 0 && f.buffer->dst_width == 992);
	/* Reversing a toggle mid-transition starts at the displayed frame. */
	dispatch_action("togglecanvas", NULL, 0);
	canvas_layout(&f.m);
	now_ms = 1652;
	anim_tick();
	content_width = f.buffer->dst_width;
	dispatch_action("togglecanvas", NULL, 0);
	resize(&f.c, tiled, 0);
	assert(abs(f.buffer->dst_width - content_width) <= 1);
	now_ms = 1677;
	anim_tick();
	content_width = f.buffer->dst_width;
	struct wlr_box reversing = f.c.rendered;
	dispatch_action("togglecanvas", NULL, 0);
	canvas_layout(&f.m);
	assert(abs(f.buffer->dst_width - content_width) <= 1);
	assert(f.c.scene->node.x == reversing.x && f.c.scene->node.y == reversing.y);
	fixture_finish(&f);
	/* A mixed multi-tag view also preserves the initial window size. */
	fixture_init(&f);
	f.m.tagset[0] = 3;
	f.m.canvas_tags = 1;
	f.m.zoom = 0.5;
	f.c.tags = 2;
	client_render(&f.c);
	dispatch_action("togglecanvas", NULL, 0);
	canvas_layout(&f.m);
	assert(f.m.canvas_tags == 2 && f.buffer->dst_width == 992);
	now_ms = 1301;
	anim_tick();
	assert(f.buffer->dst_width == (int)round(992 * CANVAS_OVERVIEW_ZOOM));
	fixture_finish(&f);
	/* Disabled animations still enter the overview immediately. */
	fixture_init(&f);
	client_render(&f.c);
	cfg.animations.enabled = false;
	dispatch_action("togglecanvas", NULL, 0);
	assert(f.m.zoom == CANVAS_OVERVIEW_ZOOM && !f.m.zoom_start);
	assert(f.buffer->dst_width == (int)round(992 * CANVAS_OVERVIEW_ZOOM));
	dispatch_action("togglecanvas", NULL, 0);
	assert(!f.c.geom_start && !f.m.canvas_tags);
	cfg.animations.enabled = true;
	fixture_finish(&f);
}

static void wallpaper(void)
{
	Fixture f;
	fixture_init(&f);
	struct wl_region *empty = wl_compositor_create_region(client_compositor);
	wl_surface_set_input_region(f.client_surface, empty);
	wl_region_destroy(empty);
	attach_buffer(&f, 992, 692);
	struct wlr_scene_buffer *native = NULL;
	wlr_scene_node_for_each_buffer(&f.c.scene_surface->node, find_surface_buffer, &native);
	assert(native && native->buffer);
	struct wlr_fbox original = native->src_box;
	struct wlr_layer_surface_v1 shell = {.surface = f.surface};
	shell.current.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
	LayerSurface bg = {.mon = &f.m, .scene = f.c.scene_surface, .mapped = 1,
		.layer_surface = &shell};
	wl_list_init(&bg.backdrop_buffers);
	wl_list_insert(&f.m.layers[0], &bg.link);
	/* As in createlayersurface(), the layer's commit listener runs before
	 * native scene helpers, so new wallpaper buffers get a fresh baseline. */
	bg.surface_commit.notify = commitlayersurfacenotify;
	wl_list_insert(&f.surface->events.commit.listener_list, &bg.surface_commit.link);
	/* Enter canvas at the user's slower speed. Wallpaper zoom is one
	 * continuous transition, with no repositioning after it completes. */
	f.m.canvas_tags = f.m.tagset[0] = 2;
	cfg.animations.speed = 0.65;
	anim_backdrop(&f.m, 1);
	anim_zoom_at(&f.m, CANVAS_OVERVIEW_ZOOM,
			f.m.m.x + f.m.m.width / 2.0, f.m.m.y + f.m.m.height / 2.0);
	double previous_width = original.width;
	for (now_ms = 1025; now_ms <= 1450; now_ms += 25) {
		anim_tick();
		assert(f.m.backdrop_offset_x == 0 && f.m.backdrop_offset_y == 0);
		assert(native->src_box.width <= previous_width);
		previous_width = native->src_box.width;
	}
	now_ms = 1462;
	assert(!anim_tick());
	struct wlr_fbox entered = native->src_box;
	now_ms = 1800;
	assert(!anim_tick());
	assert(wlr_fbox_equal(&native->src_box, &entered));
	cfg.animations.speed = 1;
	f.m.backdrop_mix = 0;
	/* Follow the actual middle-button drag path after navigating far from
	 * the origin. Parallax must remain visible throughout an infinite canvas. */
	f.m.canvas_x = 20 * f.m.m.width;
	f.m.zoom = 0.6;
	f.m.canvas_tags = f.m.tagset[0] = 2;
	backdrop_update(&f.m);
	f.m.backdrop_mix = 1;
	backdrop_update(&f.m);
	struct wlr_fbox far = native->src_box;
	panmon = &f.m;
	cursor_mode = CurCanvasPan;
	f.m.pan_start_cx = f.m.m.x + 400;
	f.m.pan_start_cy = f.m.m.y + 200;
	f.m.pan_start_ox = f.m.canvas_x;
	f.m.pan_start_oy = f.m.canvas_y;
	cursor->x = f.m.pan_start_cx - 120;
	cursor->y = f.m.pan_start_cy;
	motionnotify(0, NULL, 0, 0, 0, 0);
	assert(f.m.canvas_x == f.m.pan_start_ox + 200);
	assert(native->src_box.x > far.x + 1);
	struct wlr_fbox dragged = native->src_box;
	assert(!backdrop_motion(&f.m, now_ms)); /* A held, still drag needs no extra frames. */
	panmon = NULL;
	cursor_mode = CurNormal;
	now_ms = 3000; /* Pause while holding the button, then release. */
	pan_release(&f.m);
	now_ms = 3050;
	anim_tick();
	assert(native->src_box.x < dragged.x && native->src_box.x > far.x);
	now_ms = 4000;
	anim_tick();
	assert(wlr_fbox_equal(&native->src_box, &far));
	f.m.canvas_tags = 0;
	f.m.tagset[0] = 1;
	f.m.canvas_x = 0;
	backdrop_update(&f.m);
	struct wlr_fbox initial = native->src_box;
	assert(initial.width < 992 && initial.height < 692);
	assert(native->dst_width == 992 && native->dst_height == 692);
	f.m.canvas_x = 500;
	f.m.canvas_y = 200;
	canvas_layout(&f.m);
	assert(native->src_box.x > initial.x && native->src_box.y > initial.y);
	assert(native->src_box.x - initial.x < 500 * CANVAS_OVERVIEW_ZOOM);
	/* Repeated native wallpaper commits must not compound the crop. */
	struct wlr_fbox panned = native->src_box;
	for (int i = 0; i < 3; i++) {
		wl_surface_commit(f.client_surface);
		dispatch_requests();
		wl_event_loop_dispatch_idle(event_loop);
		assert(wlr_fbox_equal(&native->src_box, &panned));
	}
	for (int direction = -1; direction <= 1; direction += 2) {
		f.m.canvas_x = direction * 1e12;
		f.m.canvas_y = direction * 1e12;
		backdrop_update(&f.m);
		struct wlr_fbox crop = native->src_box;
		assert(crop.x >= 0 && crop.y >= 0);
		assert(crop.x + crop.width <= 992 + 1e-6);
		assert(crop.y + crop.height <= 692 + 1e-6);
		assert(native->dst_width == 992 && native->dst_height == 692);
	}
	/* Replacing the wallpaper with a different buffer size refreshes the
	 * crop baseline, rather than retaining the previous image's coordinates. */
	f.c.geom.width = f.c.bounds.width = 1208;
	f.c.geom.height = f.c.bounds.height = 808;
	f.xdg.geometry.width = f.toplevel.current.width = 1200;
	f.xdg.geometry.height = f.toplevel.current.height = 800;
	attach_buffer(&f, 1200, 800);
	wl_event_loop_dispatch_idle(event_loop);
	assert(fabs(native->src_box.width - 1200 / (1 + CANVAS_PARALLAX_OVERSCAN)) < 1e-6);
	assert(native->dst_width == 1200 && native->dst_height == 800);
	original = (struct wlr_fbox){0, 0, 1200, 800};
	/* Returning to tiling restores the exact native source and releases hooks. */
	f.m.backdrop_mix = 0;
	backdrop_update(&f.m);
	assert(wlr_fbox_equal(&native->src_box, &original));
	assert(wl_list_empty(&bg.backdrop_buffers));
	f.m.backdrop_mix = 1;
	backdrop_update(&f.m);
	pixman_region32_union_rect(&f.surface->input_region, &f.surface->input_region, 0, 0, 1, 1);
	backdrop_update(&f.m);
	assert(wlr_fbox_equal(&native->src_box, &original)); /* Interactive desktop stays native. */
	pixman_region32_clear(&f.surface->input_region);
	shell.current.layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
	backdrop_update(&f.m);
	assert(wlr_fbox_equal(&native->src_box, &original)); /* Panels are not wallpaper. */
	shell.current.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
	backdrop_update(&f.m);
	backdrop_schedule(&bg);
	backdrop_finish(&bg);
	assert(!bg.backdrop_refresh && wl_list_empty(&bg.backdrop_buffers));
	wl_list_remove(&bg.surface_commit.link);
	wl_list_remove(&bg.link);
	fixture_finish(&f);
}

int main(int argc, char **argv)
{
	int sockets[2];
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	test_client = wl_client_create(dpy, sockets[0]);
	if (!test_client)
		perror("wl_client_create");
	assert(test_client);
	struct wlr_renderer *renderer = wlr_pixman_renderer_create();
	assert(renderer);
	assert(wlr_renderer_init_wl_shm(renderer, dpy));
	compositor = wlr_compositor_create(dpy, 6, renderer);
	struct wl_listener new_surface = {.notify = new_surface_test};
	wl_signal_add(&compositor->events.new_surface, &new_surface);
	client_display = wl_display_connect_to_fd(sockets[1]);
	assert(client_display);
	struct wl_registry *registry = wl_display_get_registry(client_display);
	wl_registry_add_listener(registry, &registry_listener_test, NULL);
	dispatch_requests();
	assert(wl_display_dispatch(client_display) >= 0);
	assert(client_compositor);
	assert(client_shm);
	seat = wlr_seat_create(dpy, "test-seat");
	assert(seat);
	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&mons);
	wl_list_init(&snapshots);
	wl_list_init(&ipc_managers);
	wl_list_init(&ext_workspaces);
	scene = wlr_scene_create();
	for (int i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	cursor = wlr_cursor_create();
	assert(cursor);
	cfg.animations.enabled = true;
	cfg.animations.speed = 1;
	if (argc == 1 || !strcmp(argv[1], "workspace")) workspace();
	if (argc == 1 || !strcmp(argv[1], "opening")) opening();
	if (argc == 1 || !strcmp(argv[1], "closing")) closing();
	if (argc == 1 || !strcmp(argv[1], "sending")) sending();
	if (argc == 1 || !strcmp(argv[1], "resizing")) resizing();
	if (argc == 1 || !strcmp(argv[1], "redraw")) redraw();
	if (argc == 1 || !strcmp(argv[1], "canvas")) canvas_toggle();
	if (argc == 1 || !strcmp(argv[1], "wallpaper")) wallpaper();
	wlr_scene_node_destroy(&scene->tree.node);
	wlr_cursor_destroy(cursor);
	wl_list_remove(&new_surface.link);
	wl_registry_destroy(registry);
	wl_compositor_destroy(client_compositor);
	wl_shm_destroy(client_shm);
	wl_display_disconnect(client_display);
	wl_display_destroy_clients(dpy);
	wl_display_destroy(dpy);
	wlr_renderer_destroy(renderer);
	puts("animation regressions passed");
	return 0;
}
